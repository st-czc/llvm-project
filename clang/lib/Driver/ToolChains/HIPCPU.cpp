//===--- HIPCPU.cpp - HIP spvcpu ToolChain Impl -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HIPCPU.h"
#include "HIPUtility.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Options/Options.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/TargetParser/Host.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

void HIPCPU::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const ArgList &Args,
                                  const char *LinkingOutput) const {
  if (JA.getType() == types::TY_HIP_FATBIN)
    return HIP::constructHIPFatbinCommand(C, JA, Output.getFilename(), Inputs,
                                          Args, *this);

  if (JA.getType() == types::TY_LLVM_BC)
    return constructLLVMLinkCommand(C, JA, Inputs, Output, Args);

  return constructLinkAndEmitDylibCommand(C, JA, Inputs, Output, Args);
}

void HIPCPU::Linker::constructLLVMLinkCommand(Compilation &C,
                                              const JobAction &JA,
                                              const InputInfoList &Inputs,
                                              const InputInfo &Output,
                                              const ArgList &Args) const {
  ArgStringList LinkerInputs;

  for (auto Input : Inputs)
    if (Input.isFilename())
      LinkerInputs.push_back(Input.getFilename());

  tools::constructLLVMLinkCommand(C, *this, JA, Inputs, LinkerInputs, Output,
                                  Args);
}

void HIPCPU::Linker::constructLinkAndEmitDylibCommand(
    Compilation &C, const JobAction &JA, const InputInfoList &Inputs,
    const InputInfo &Output, const ArgList &Args) const {
  assert(!Inputs.empty() && "expected device bitcode inputs");

  const ToolChain &TC = getToolChain();
  const Driver &D = C.getDriver();
  const auto &HIPTC = static_cast<const HIPCPUToolChain &>(TC);

  const char *LLVMLink = Args.MakeArgString(TC.GetProgramPath("llvm-link"));
  const char *Opt = Args.MakeArgString(TC.GetProgramPath("opt"));
  const std::string Stem = llvm::sys::path::stem(Output.getFilename()).str();

  std::string HostTriple =
      Args.getLastArgValue(options::OPT_hip_cpu_host_triple_EQ).str();
  if (HostTriple.empty())
    HostTriple = llvm::sys::getDefaultTargetTriple();

  // Merge the bitcode of every translation unit.
  const char *MergedFile = HIP::getTempFile(C, Stem + "-merged", "bc");
  InputInfo Merged(&JA, MergedFile, Output.getBaseInput());
  {
    ArgStringList LinkArgs{"-o", MergedFile, "-suppress-warnings"};
    for (const InputInfo &Input : Inputs)
      if (Input.isFilename())
        LinkArgs.push_back(Input.getFilename());
    C.addCommand(std::make_unique<Command>(JA, *this,
                                           ResponseFileSupport::None(),
                                           LLVMLink, LinkArgs, Inputs, Merged));
  }

  // Link the CPU support library. It defines symbols that the lowering passes
  // introduce, not symbols referenced by the source.
  const char *UtilsFile = HIP::getTempFile(C, Stem + "-utils", "bc");
  InputInfo UtilsLinked(&JA, UtilsFile, Output.getBaseInput());
  {
    ArgStringList LinkArgs{"-o", UtilsFile, "-suppress-warnings", MergedFile,
                           Args.MakeArgString(HIPTC.getUtilsLibPath())};
    C.addCommand(
        std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                  LLVMLink, LinkArgs, Merged, UtilsLinked));
  }

  // Lower the linked module to the native CPU execution model.
  const char *LoweredFile = HIP::getTempFile(C, Stem + "-lowered", "bc");
  InputInfo Lowered(&JA, LoweredFile, Output.getBaseInput());
  {
    ArgStringList OptArgs{UtilsFile,
                          Args.MakeArgString("-mtriple=" + HostTriple),
                          "-load-pass-plugin",
                          Args.MakeArgString(HIPTC.getPassesPluginPath()),
                          "-passes=default<O2>,sycl-native-cpu",
                          "-o",
                          LoweredFile};
    C.addCommand(std::make_unique<Command>(JA, *this,
                                           ResponseFileSupport::None(), Opt,
                                           OptArgs, UtilsLinked, Lowered));
  }

  // The device image is always linked as a shared library. clang-linker-wrapper
  // passes -shared for CPU offloading, and it is consumed here.
  Args.ClaimAllArgs(options::OPT_shared);

  // Generate host machine code and link the shared library.
  ArgStringList ClangArgs{"-target",
                          Args.MakeArgString(HostTriple),
                          "-O2",
                          "-fPIC",
                          "-shared",
                          "-Wl,-Bsymbolic",
                          "-x",
                          "ir",
                          LoweredFile,
                          "-o",
                          Output.getFilename()};
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         D.getDriverProgramPath(), ClangArgs,
                                         Lowered, Output, D.getPrependArg()));
}

HIPCPUToolChain::HIPCPUToolChain(const Driver &D, const llvm::Triple &Triple,
                                 const ToolChain &HostTC, const ArgList &Args)
    : ToolChain(D, Triple, Args), HostTC(&HostTC),
      RocmInstallation(D, HostTC.getTriple(), Args) {
  getProgramPaths().push_back(D.Dir);
}

HIPCPUToolChain::HIPCPUToolChain(const Driver &D, const llvm::Triple &Triple,
                                 const ArgList &Args)
    : ToolChain(D, Triple, Args), HostTC(nullptr),
      RocmInstallation(D, Triple, Args) {
  getProgramPaths().push_back(D.Dir);
}

Tool *HIPCPUToolChain::buildLinker() const { return new HIPCPU::Linker(*this); }

std::string HIPCPUToolChain::getPassesPluginPath() const {
  llvm::SmallString<256> Path = llvm::sys::path::parent_path(getDriver().Dir);
  llvm::sys::path::append(Path, "lib", "LLVMHIPOnSYCLNativeCPUPasses.so");
  return Path.str().str();
}

std::string HIPCPUToolChain::getUtilsLibPath() const {
  llvm::SmallString<256> Path(getDriver().Dir);
  llvm::sys::path::append(Path, "..", "spvcpu", "bitcode",
                          "libsycl-nativecpu_utils.bc");
  if (llvm::sys::fs::exists(Path))
    return Path.str().str();

  Path = getDriver().Dir;
  llvm::sys::path::append(Path, "..", "..", "..");
  llvm::sys::path::append(Path, "spvcpu", "bitcode",
                          "libsycl-nativecpu_utils.bc");
  return Path.str().str();
}

void HIPCPUToolChain::addClangTargetOptions(
    const ArgList &DriverArgs, ArgStringList &CC1Args, BoundArch BA,
    Action::OffloadKind DeviceOffloadKind) const {
  if (!HostTC)
    return;

  HostTC->addClangTargetOptions(DriverArgs, CC1Args, BA, DeviceOffloadKind);
  CC1Args.push_back("-fcuda-is-device");

  if (!DriverArgs.hasArg(options::OPT_fvisibility_EQ,
                         options::OPT_fvisibility_ms_compat))
    CC1Args.append(
        {"-fvisibility=hidden", "-fapply-global-visibility-to-externs"});
}

void HIPCPUToolChain::addClangWarningOptions(ArgStringList &CC1Args) const {
  if (HostTC)
    HostTC->addClangWarningOptions(CC1Args);
}

ToolChain::CXXStdlibType
HIPCPUToolChain::GetCXXStdlibType(const ArgList &Args) const {
  if (HostTC)
    return HostTC->GetCXXStdlibType(Args);
  return ToolChain::GetCXXStdlibType(Args);
}

void HIPCPUToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                                ArgStringList &CC1Args) const {
  if (HostTC)
    HostTC->AddClangSystemIncludeArgs(DriverArgs, CC1Args);
}

void HIPCPUToolChain::AddClangCXXStdlibIncludeArgs(
    const ArgList &Args, ArgStringList &CC1Args) const {
  if (HostTC)
    HostTC->AddClangCXXStdlibIncludeArgs(Args, CC1Args);
}

void HIPCPUToolChain::AddHIPIncludeArgs(const ArgList &DriverArgs,
                                        ArgStringList &CC1Args) const {
  RocmInstallation->AddHIPIncludeArgs(DriverArgs, CC1Args);
}

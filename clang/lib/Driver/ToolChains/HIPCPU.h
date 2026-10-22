//===--- HIPCPU.h - HIP spvcpu ToolChain -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HIPCPU_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HIPCPU_H

#include "clang/Driver/Driver.h"
#include "clang/Driver/LazyDetector.h"
#include "clang/Driver/RocmInstallationDetector.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace tools {

namespace HIPCPU {
// Runs llvm-link/opt/clang, which links multiple LLVM bitcode, together with
// the SPVCPU device library, then compiles it to a shared object.
class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("HIPCPU::Linker", "hipcpu-link", TC) {}

  bool hasIntegratedCPP() const override { return false; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;

private:
  void constructLLVMLinkCommand(Compilation &C, const JobAction &JA,
                                const InputInfoList &Inputs,
                                const InputInfo &Output,
                                const llvm::opt::ArgList &Args) const;
  void constructLinkAndEmitDylibCommand(Compilation &C, const JobAction &JA,
                                        const InputInfoList &Inputs,
                                        const InputInfo &Output,
                                        const llvm::opt::ArgList &Args) const;
};

} // end namespace HIPCPU
} // namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY HIPCPUToolChain final : public ToolChain {
public:
  HIPCPUToolChain(const Driver &D, const llvm::Triple &Triple,
                  const ToolChain &HostTC, const llvm::opt::ArgList &Args);
  HIPCPUToolChain(const Driver &D, const llvm::Triple &Triple,
                  const llvm::opt::ArgList &Args);

  const llvm::Triple *getAuxTriple() const override {
    return HostTC ? &HostTC->getTriple() : nullptr;
  }

  /// Returns the path to the pass plugin used to lower the device module.
  std::string getPassesPluginPath() const;

  /// Returns the path to the NativeCPU support bitcode library.
  std::string getUtilsLibPath() const;

  void
  addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                        llvm::opt::ArgStringList &CC1Args, BoundArch BA,
                        Action::OffloadKind DeviceOffloadKind) const override;
  void addClangWarningOptions(llvm::opt::ArgStringList &CC1Args) const override;
  CXXStdlibType GetCXXStdlibType(const llvm::opt::ArgList &Args) const override;
  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;
  void AddClangCXXStdlibIncludeArgs(
      const llvm::opt::ArgList &Args,
      llvm::opt::ArgStringList &CC1Args) const override;
  void AddHIPIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                         llvm::opt::ArgStringList &CC1Args) const override;

  /// Device compilation always stops at bitcode; code generation happens once
  /// the whole device module has been linked and lowered.
  LTOKind getDefaultLTOMode() const override { return LTOK_Full; }

  // The device image is a shared library.
  bool isPICDefault() const override { return true; }
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override {
    return false;
  }
  bool isPICDefaultForced() const override { return true; }

protected:
  Tool *buildLinker() const override;

private:
  const ToolChain *HostTC;
  LazyDetector<RocmInstallationDetector> RocmInstallation;
};

} // namespace toolchains
} // namespace driver
} // namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_HIPCPU_H

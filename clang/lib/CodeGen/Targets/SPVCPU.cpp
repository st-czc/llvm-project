//===- SPVCPU.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"
#include "clang/AST/Expr.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::CodeGen;

namespace {
class SPVCPUABIInfo : public DefaultABIInfo {
private:
  const ABIInfo *HostABIInfo;

public:
  SPVCPUABIInfo(CodeGen::CodeGenTypes &CGT, const ABIInfo *HostABIInfo)
      : DefaultABIInfo(CGT), HostABIInfo(HostABIInfo) {}

  void computeInfo(CGFunctionInfo &FI) const override;
};

class SPVCPUTargetCodeGenInfo : public TargetCodeGenInfo {
private:
  std::unique_ptr<TargetCodeGenInfo> HostTargetCodeGenInfo;

public:
  SPVCPUTargetCodeGenInfo(
      CodeGen::CodeGenTypes &CGT,
      std::unique_ptr<TargetCodeGenInfo> HostTargetCodeGenInfo)
      : TargetCodeGenInfo(std::make_unique<SPVCPUABIInfo>(
            CGT, HostTargetCodeGenInfo ? &HostTargetCodeGenInfo->getABIInfo()
                                       : nullptr)),
        HostTargetCodeGenInfo(std::move(HostTargetCodeGenInfo)) {}

  void setFunctionDeclAttributes(const FunctionDecl *FD, llvm::Function *F,
                                 CodeGenModule &CGM) const;

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &M) const override;

  unsigned getDeviceKernelCallingConv() const override {
    return llvm::CallingConv::SPIR_KERNEL;
  }

  void setCUDAKernelCallingConvention(const FunctionType *&FT) const override {
    FT = getABIInfo().getContext().adjustFunctionType(
        FT, FT->getExtInfo().withCallingConv(CC_DeviceKernel));
  }
};
} // namespace

void SPVCPUTargetCodeGenInfo::setFunctionDeclAttributes(
    const FunctionDecl *FD, llvm::Function *F, CodeGenModule &M) const {
  const auto *ReqdWGS =
      M.getLangOpts().OpenCL ? FD->getAttr<ReqdWorkGroupSizeAttr>() : nullptr;
  const bool IsHIPKernel = M.getLangOpts().HIP && FD->hasAttr<CUDAGlobalAttr>();

  const auto *FlatWGS = FD->getAttr<AMDGPUFlatWorkGroupSizeAttr>();

  // __launch_bounds__ only takes effect on kernels and is silently ignored on
  // other functions The arguments are honored only if the equivalent native
  // amdgpu_flat_work_group_size / amdgpu_waves_per_eu attribute was not also
  // used out; those take precedence.
  const auto *LaunchBounds =
      IsHIPKernel ? FD->getAttr<CUDALaunchBoundsAttr>() : nullptr;
  unsigned LBMaxThreads = 0;
  unsigned LBMinWaves = 0;
  if (LaunchBounds) {
    LBMaxThreads = LaunchBounds->getMaxThreads()
                       ->EvaluateKnownConstInt(M.getContext())
                       .getExtValue();
    if (const Expr *MinBlocks = LaunchBounds->getMinBlocks()) {
      LBMinWaves =
          MinBlocks->EvaluateKnownConstInt(M.getContext()).getExtValue();
    }
  }

  if (ReqdWGS || FlatWGS) {
    M.handleAMDGPUFlatWorkGroupSizeAttr(F, FlatWGS, ReqdWGS);
  } else if (LBMaxThreads > 0) {
    F->addFnAttr("amdgpu-flat-work-group-size",
                 "1," + llvm::utostr(LBMaxThreads));
  } else if (IsHIPKernel) {
    // By default, restrict the maximum size to a value specified by
    // --gpu-max-threads-per-block=n or its default value for HIP.
    std::string AttrVal =
        std::string("1,") + llvm::utostr(M.getLangOpts().GPUMaxThreadsPerBlock);
    F->addFnAttr("amdgpu-flat-work-group-size", AttrVal);
  }

  if (const auto *Attr = FD->getAttr<AMDGPUWavesPerEUAttr>()) {
    M.handleAMDGPUWavesPerEUAttr(F, Attr);
  } else if (LBMinWaves > 0) {
    // HIP reinterprets the second argument as the minimum waves per EU.
    //
    // TODO: The third argument (maxclusterrank) could be used if the AMDGPU
    // "clusters" feature is supported for the current subtarget.
    F->addFnAttr("amdgpu-waves-per-eu", llvm::utostr(LBMinWaves));
  }

  if (const auto *Attr = FD->getAttr<AMDGPUNumSGPRAttr>()) {
    unsigned NumSGPR = Attr->getNumSGPR();

    if (NumSGPR != 0)
      F->addFnAttr("amdgpu-num-sgpr", llvm::utostr(NumSGPR));
  }

  if (const auto *Attr = FD->getAttr<AMDGPUNumVGPRAttr>()) {
    uint32_t NumVGPR = Attr->getNumVGPR();

    if (NumVGPR != 0)
      F->addFnAttr("amdgpu-num-vgpr", llvm::utostr(NumVGPR));
  }

  if (const auto *Attr = FD->getAttr<AMDGPUMaxNumWorkGroupsAttr>()) {
    uint32_t X = Attr->getMaxNumWorkGroupsX()
                     ->EvaluateKnownConstInt(M.getContext())
                     .getExtValue();
    // Y and Z dimensions default to 1 if not specified
    uint32_t Y = Attr->getMaxNumWorkGroupsY()
                     ? Attr->getMaxNumWorkGroupsY()
                           ->EvaluateKnownConstInt(M.getContext())
                           .getExtValue()
                     : 1;
    uint32_t Z = Attr->getMaxNumWorkGroupsZ()
                     ? Attr->getMaxNumWorkGroupsZ()
                           ->EvaluateKnownConstInt(M.getContext())
                           .getExtValue()
                     : 1;

    llvm::SmallString<32> AttrVal;
    llvm::raw_svector_ostream OS(AttrVal);
    OS << X << ',' << Y << ',' << Z;

    F->addFnAttr("amdgpu-max-num-workgroups", AttrVal.str());
  }

  if (auto *Attr = FD->getAttr<CUDAClusterDimsAttr>()) {
    auto GetExprVal = [&](const auto &E) {
      return E ? E->EvaluateKnownConstInt(M.getContext()).getExtValue() : 1;
    };
    unsigned X = GetExprVal(Attr->getX());
    unsigned Y = GetExprVal(Attr->getY());
    unsigned Z = GetExprVal(Attr->getZ());
    llvm::SmallString<32> AttrVal;
    llvm::raw_svector_ostream OS(AttrVal);
    OS << X << ',' << Y << ',' << Z;
    F->addFnAttr("amdgpu-cluster-dims", AttrVal.str());
  }

  if (FD->hasAttr<CUDANoClusterAttr>())
    F->addFnAttr("amdgpu-cluster-dims", "0,0,0");
}

void SPVCPUTargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &M) const {
  if (GV->isDeclaration())
    return;

  llvm::Function *F = dyn_cast<llvm::Function>(GV);
  if (!F)
    return;

  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D);
  if (FD)
    setFunctionDeclAttributes(FD, F, M);
}

void SPVCPUABIInfo::computeInfo(CGFunctionInfo &FI) const {
  // A function with CC_DeviceKernel never reaches this function.
  // SPVCPU maps CC_DeviceKernel to SPIR_KERNEL. computeSPIRKernelABIInfo
  // computes the ABI before this function runs. CommonSPIRABIInfo supplies
  // the argument rules.
  //
  // The default path below runs only when no host ABI exists.
  // If a compiler-internal function must use the default ABI even when a
  // host ABI exists, add a dedicated calling convention. intel/llvm does
  // this with CC_NativeCPUFunction. Then change the host branch above to
  // skip that function.

  if (HostABIInfo) {
    HostABIInfo->computeInfo(FI);
    return;
  }

  DefaultABIInfo::computeInfo(FI);
  FI.setEffectiveCallingConvention(llvm::CallingConv::C);
}

std::unique_ptr<TargetCodeGenInfo> CodeGen::createSPVCPUTargetCodeGenInfo(
    CodeGenModule &CGM,
    std::unique_ptr<TargetCodeGenInfo> HostTargetCodeGenInfo) {
  return std::make_unique<SPVCPUTargetCodeGenInfo>(
      CGM.getTypes(), std::move(HostTargetCodeGenInfo));
}

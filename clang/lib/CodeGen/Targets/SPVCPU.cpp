//===- SPVCPU.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

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

  unsigned getDeviceKernelCallingConv() const override {
    return llvm::CallingConv::SPIR_KERNEL;
  }

  void setCUDAKernelCallingConvention(const FunctionType *&FT) const override {
    FT = getABIInfo().getContext().adjustFunctionType(
        FT, FT->getExtInfo().withCallingConv(CC_DeviceKernel));
  }
};
} // namespace

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

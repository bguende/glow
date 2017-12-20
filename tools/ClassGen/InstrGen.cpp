// Copyright 2017 Facebook Inc.  All Rights Reserved.
#include "InstrBuilder.h"

#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 5) {
    std::cerr << "Usage: " << argv[0]
              << " header.h impl.cpp enums.def irbuilder.h\n";
    return -1;
  }

  std::cout << "Writing instr descriptors to:\n\t" << argv[1] << "\n\t"
            << argv[2] << "\n\t" << argv[3] << "\n\t" << argv[4] << "\n";

  std::ofstream headerStream(argv[1]);
  std::ofstream cppStream(argv[2]);
  std::ofstream defStream(argv[3]);
  std::ofstream builderStream(argv[4]);

  Builder BB(headerStream, cppStream, defStream, builderStream);

  //===--------------------------------------------------------------------===//
  //               Memory / Buffer Management
  //===--------------------------------------------------------------------===//

  BB.declareValue("WeightVar");

  BB.newInstr("AllocActivation")
      .addMember(MemberType::TypeRef, "Ty")
      .setType("Ty");

  BB.newInstr("TensorView")
      .addOperand("Src", OperandKind::In)
      .addMember(MemberType::TypeRef, "Ty")
      .setType("Ty");

  BB.newInstr("DeallocActivation")
      .addOperand("Src", OperandKind::Out)
      .addExtraMethod("AllocActivationInst *getAlloc() const { return "
                      "llvm::cast<AllocActivationInst>(getOperand(0)."
                      "first); }")
      .setType("Src->getType()");

  BB.newInstr("Copy")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Src", OperandKind::In)
      .setType("Src->getType()");

  //===--------------------------------------------------------------------===//
  //                   Convolution / Pool / FC
  //===--------------------------------------------------------------------===//

  BB.newInstr("Convolution")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Src", OperandKind::In)
      .addOperand("Filter", OperandKind::In)
      .addOperand("Bias", OperandKind::In)
      .addMember(MemberType::SizeT, "Kernel")
      .addMember(MemberType::SizeT, "Stride")
      .addMember(MemberType::SizeT, "Pad")
      .addMember(MemberType::SizeT, "Depth")
      .addGradientInstr({"Src", "Filter"}, {"Dest", "Src", "Filter", "Bias"});

  BB.newInstr("PoolMax")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Src", OperandKind::In)
      .addOperand("SrcXY", OperandKind::InOut)
      .addMember(MemberType::SizeT, "Kernel")
      .addMember(MemberType::SizeT, "Stride")
      .addMember(MemberType::SizeT, "Pad")
      .addGradientInstr({"Dest", "SrcXY"}, {"Dest", "Src"});

  BB.newInstr("PoolAvg")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Src", OperandKind::In)
      .addMember(MemberType::SizeT, "Kernel")
      .addMember(MemberType::SizeT, "Stride")
      .addMember(MemberType::SizeT, "Pad")
      .addGradientInstr({"Dest"}, {"Dest", "Src"});

  BB.newInstr("FullyConnected")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Src", OperandKind::In)
      .addOperand("Filter", OperandKind::In)
      .addOperand("Bias", OperandKind::In)
      .addMember(MemberType::SizeT, "Depth")
      .addGradientInstr({"Src", "Filter"}, {"Dest", "Src", "Filter", "Bias"});

  //===--------------------------------------------------------------------===//
  //                     Normalization
  //===--------------------------------------------------------------------===//

  BB.newInstr("BatchNormalization")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Src", OperandKind::In)
      .addOperand("Scale", OperandKind::In)
      .addOperand("Bias", OperandKind::In)
      .addOperand("Mean", OperandKind::In)
      .addOperand("Var", OperandKind::In)
      .addMember(MemberType::SizeT, "ChannelIdx")
      .addMember(MemberType::Float, "Epsilon")
      .addMember(MemberType::Float, "Momentum")
      .inplaceOperand({
          "Dest",
          "Src",
      })
      .addGradientInstr({"Src", "Scale", "Mean", "Var"},
                        {"Dest", "Src", "Scale", "Bias"});

  BB.newInstr("LocalResponseNormalization")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Src", OperandKind::In)
      .addOperand("Scale", OperandKind::Out)
      .addMember(MemberType::SizeT, "HalfWindowSize")
      .addMember(MemberType::Float, "Alpha")
      .addMember(MemberType::Float, "Beta")
      .addMember(MemberType::Float, "K")
      .setType("Src->getType()")
      .inplaceOperand({
          "Dest",
          "Src",
      })
      .addGradientInstr({"Dest", "Src", "Scale"}, {"Dest", "Src"});

  //===--------------------------------------------------------------------===//
  //                      Loss operations
  //===--------------------------------------------------------------------===//

  BB.newInstr("SoftMax")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Src", OperandKind::In)
      .addOperand("E", OperandKind::InOut)
      .addOperand("Selected", OperandKind::In)
      .inplaceOperand({"Dest", "Src"})
      .addGradientInstr({"Src", "E", "Selected"}, {"Src"});

  //===--------------------------------------------------------------------===//
  //                      Arithmetic
  //===--------------------------------------------------------------------===//

  /// Perform matrix multiplication between the matrix Filter and all matrices
  /// in the batch Batch. Filter is a two dimensional matrix, and Batch is a
  /// three dimensional matrix where the first dimension is the batch size. The
  /// result is a tensor that has the same batch size as the Batch parameter,
  /// and the second and third dimensions are the size of the multiplied matrix.
  BB.newInstr("BatchedMatMul")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Batch", OperandKind::In)
      .addOperand("Filter", OperandKind::In);

  /// Accumulates all of the layers in the batch and produce a tensor that has
  /// the same dimensions as the input tensor without the first dimension.
  BB.newInstr("BatchedReduceAdd")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Batch", OperandKind::In);

  /// Adds the 'Slice' operand to each one of the slices in the batch.
  BB.newInstr("BatchedAdd")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Batch", OperandKind::In)
      .addOperand("Slice", OperandKind::In)
      .inplaceOperand({"Dest", "Batch"});

  BB.newInstr("ElementAdd")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("LHS", OperandKind::In)
      .addOperand("RHS", OperandKind::In)
      .inplaceOperand({"Dest", "LHS", "RHS"})
      .addGradientInstr({}, {"Dest", "LHS", "RHS"});

  BB.newInstr("ElementSub")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("LHS", OperandKind::In)
      .addOperand("RHS", OperandKind::In)
      .inplaceOperand({"Dest", "LHS", "RHS"})
      .addGradientInstr({}, {"Dest", "LHS", "RHS"});

  BB.newInstr("ElementMul")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("LHS", OperandKind::In)
      .addOperand("RHS", OperandKind::In)
      .inplaceOperand({"Dest", "LHS", "RHS"})
      .addGradientInstr({"LHS", "RHS"}, {"Dest", "LHS", "RHS"});

  BB.newInstr("ElementDiv")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("LHS", OperandKind::In)
      .addOperand("RHS", OperandKind::In)
      .inplaceOperand({"Dest", "LHS", "RHS"})
      .addGradientInstr({"LHS", "RHS"}, {"Dest", "LHS", "RHS"});

  //===--------------------------------------------------------------------===//
  //                Non-linearities
  //===--------------------------------------------------------------------===//

  BB.newInstr("Relu")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Src", OperandKind::In)
      .inplaceOperand({
          "Dest",
          "Src",
      })
      .addGradientInstr({"Dest"}, {"Dest", "Src"});

  BB.newInstr("Sigmoid")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Src", OperandKind::In)
      .inplaceOperand({
          "Dest",
          "Src",
      })
      .addGradientInstr({"Dest"}, {"Dest", "Src"});

  BB.newInstr("Tanh")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Src", OperandKind::In)
      .inplaceOperand({
          "Dest",
          "Src",
      })
      .addGradientInstr({"Dest"}, {"Dest", "Src"});

  //===--------------------------------------------------------------------===//
  //                Shape transformations
  //===--------------------------------------------------------------------===//

  BB.newInstr("Reshape")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Src", OperandKind::In)
      .addMember(MemberType::VectorSizeT, "Dims");

  BB.newInstr("Transpose")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Src", OperandKind::In)
      .addMember(MemberType::VectorUnsigned, "Shuffle");

  BB.newInstr("Splat")
      .addMember(MemberType::Float, "Value")
      .addOperand("Dest", OperandKind::Out);

  BB.newInstr("InsertTensor")
      .addOperand("Dest", OperandKind::InOut)
      .addOperand("Src", OperandKind::In)
      .addMember(MemberType::VectorSizeT, "Offsets");

  BB.newInstr("ExtractTensor")
      .addOperand("Dest", OperandKind::Out)
      .addOperand("Src", OperandKind::In)
      .addMember(MemberType::VectorSizeT, "Offsets");

  //===--------------------------------------------------------------------===//
  //             Instructions used for network training
  //===--------------------------------------------------------------------===//

  BB.newInstr("SGD")
      .addOperand("Gradient", OperandKind::In)
      .addOperand("Weight", OperandKind::InOut)
      .addOperand("Gsum", OperandKind::InOut)
      .addMember(MemberType::Float, "L1Decay")
      .addMember(MemberType::Float, "L2Decay")
      .addMember(MemberType::Float, "LearningRate")
      .addMember(MemberType::Float, "Momentum")
      .addMember(MemberType::Unsigned, "BatchSize");

  //===--------------------------------------------------------------------===//
  //             Instructions used for debugging/profiling/printing
  //===--------------------------------------------------------------------===//

  BB.newInstr("DebugPrint").addOperand("Src", OperandKind::In);

  return 0;
}

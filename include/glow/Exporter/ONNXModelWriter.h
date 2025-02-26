/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef GLOW_EXPORTER_ONNXMODELWRITER_H
#define GLOW_EXPORTER_ONNXMODELWRITER_H

#include "glow/Exporter/CommonOperatorWriter.h"
#include "glow/Graph/Graph.h"

#include "onnx/onnx_pb.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include <string>

/// ONNX traits for protobuf types.
struct ONNX_TRAITS {
  using GraphProto = ONNX_NAMESPACE::GraphProto;
};

namespace glow {

/// Unique set of visited nodes.
using ReportedNodes = std::unordered_set<const Node *>;

/// Writes ONNX models.
class ONNXModelWriter : public CommonOperatorWriter<ONNX_TRAITS> {
  // Declare shorter aliases.
  using GraphType = typename ONNX_TRAITS::GraphProto;
  using NodeType = ONNX_NAMESPACE::NodeProto;
  using TensorType = ONNX_NAMESPACE::TensorProto;
  using AttrType = ONNX_NAMESPACE::AttributeProto;
  using ValueInfoType = ONNX_NAMESPACE::ValueInfoProto;

  /// Current version of ONNX standard.
  size_t opsetVersion_;
  /// Keeps the track of already visited or processed nodes.
  ReportedNodes reportedNodes_;
  /// Writes tensor shape from placeholder \p PH into protpbuf \p valueProto.
  static void tensorShapeFromPlaceholder(const Placeholder *PH,
                                         ValueInfoType *valueProto);
  /// Writes all inputs and outputs with operator name \p opName from give Node
  /// \p node into protobuf \p proto.
  static llvm::Error writeAllWithNode(const std::string &opName,
                                      const Node *node, NodeType *proto);
  /// Writes all inputs and outputs with operator name \p opName from give Node
  /// \p node into created node protobuf using \p graph.
  static llvm::Error writeAll(const std::string &opName, const Node *node,
                              GraphType &graph);
  // Finds if uses of \p node have node with the provided \p kind.
  static bool hasUsesOfKind(const Node *node, Kinded::Kind kind);

public:
  /// Converts \p glowType to \p protoType.
  static typename TensorType::DataType convertType(const Type &glowType);
  /// Writes Glow tensor \p T to proto output \p out.
  static void writeTensor(const Tensor &T, TensorType *out);

  /// Creates an ONNX model writer to serialize \p F graph into file
  /// \p modelFilename, writing \p irVersion and \p opsetVersion.
  /// If \p errPtr is not null then if an error occurs it will get assigned
  /// there otherwise if an error occurs it will abort.
  ONNXModelWriter(const std::string &modelFilename, Function &F,
                  size_t irVersion, size_t opsetVersion,
                  llvm::Error *errPtr = nullptr, bool textMode = false);

private:
  /// \returns error for the unexpected node kind.
  static llvm::Error writeUnexpectedKind(const Node *node) {
    RETURN_ERR(strFormat("Glow can not export node %s, unsupported kind: %s.",
                         node->getName().str().c_str(), node->getKindName()));
  }

  /// Declares the overriden all pure virtual methods, declared in base class.
#define DEF_NODE(CLASS, NAME)                                                  \
  llvm::Error write##NAME(const CLASS *, GraphType &) override;
#include "glow/AutoGenNodes.def"
};

} // namespace glow

#endif // GLOW_EXPORTER_ONNXMODELWRITER_H

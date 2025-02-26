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

#include "TorchGlowTraining.h"
#include <gtest/gtest.h>

#ifndef GLOW_DATA_PATH
#define GLOW_DATA_PATH
#endif

using namespace glow;

TEST(TorchGlowTraining, Test) {
  const std::string fileName{GLOW_DATA_PATH
                             "tests/models/pytorchModels/resnet18.pt"};
  TorchGlowTraining trainer;
  std::vector<torch::jit::IValue> vec;
  auto emptyTensor = at::empty({1, 3, 224, 224});
  vec.push_back(torch::autograd::make_variable(emptyTensor));
  TorchGlowTraining::ONNXWriterParameters parameters;
  TrainingConfig config;
  config.learningRate = 0.01;
  config.momentum = 0.9;
  config.L2Decay = 0.01;
  config.batchSize = 1;

  // TODO (after full fusion is available)
  if (errToBool(
          trainer.init(fileName, vec, "Interpreter", parameters, config))) {
    return;
  }

  std::vector<size_t> sampleDims = {1, 3, 224, 224};
  Tensor samples(ElemKind::FloatTy, sampleDims);

  std::vector<size_t> labelDims = {1, 1000};
  Tensor labels(ElemKind::Int64ITy, labelDims);
  EXPECT_FALSE(errToBool(trainer.train(samples, labels)));

  EXPECT_FALSE(errToBool(trainer.save("/tmp/test.onnx")));
}

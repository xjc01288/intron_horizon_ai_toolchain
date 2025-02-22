// Copyright (c) 2020 Horizon Robotics.All Rights Reserved.
//
// The material in this file is confidential and contains trade secrets
// of Horizon Robotics Inc. This is proprietary information owned by
// Horizon Robotics Inc. No part of this work may be disclosed,
// reproduced, copied, transmitted, or used in any way for any purpose,
// without the express written permission of Horizon Robotics Inc.

#include "method/ptq_efficientdet_post_process_method.h"

#include <queue>

#include "method/method_data.h"
#include "method/method_factory.h"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "utils/algorithm.h"
#include "utils/nms.h"
#include "utils/tensor_utils.h"

DEFINE_AND_REGISTER_METHOD(PTQEfficientDetPostProcessMethod);

const int kEfficientDetClassNum = 80;

EfficientDetConfig default_efficient_det_config = {
    {{4.0, 5.039684199579493, 6.3496042078727974},
     {4.0, 5.039684199579493, 6.3496042078727974},
     {4.0, 5.039684199579493, 6.3496042078727974},
     {4.0, 5.039684199579493, 6.3496042078727974},
     {4.0, 5.039684199579493, 6.3496042078727974}},
    {0.5, 1, 2},
    {8, 16, 32, 64, 128},
    80,
    {"person",        "bicycle",      "car",
     "motorcycle",    "airplane",     "bus",
     "train",         "truck",        "boat",
     "traffic light", "fire hydrant", "stop sign",
     "parking meter", "bench",        "bird",
     "cat",           "dog",          "horse",
     "sheep",         "cow",          "elephant",
     "bear",          "zebra",        "giraffe",
     "backpack",      "umbrella",     "handbag",
     "tie",           "suitcase",     "frisbee",
     "skis",          "snowboard",    "sports ball",
     "kite",          "baseball bat", "baseball glove",
     "skateboard",    "surfboard",    "tennis racket",
     "bottle",        "wine glass",   "cup",
     "fork",          "knife",        "spoon",
     "bowl",          "banana",       "apple",
     "sandwich",      "orange",       "broccoli",
     "carrot",        "hot dog",      "pizza",
     "donut",         "cake",         "chair",
     "couch",         "potted plant", "bed",
     "dining table",  "toilet",       "tv",
     "laptop",        "mouse",        "remote",
     "keyboard",      "cell phone",   "microwave",
     "oven",          "toaster",      "sink",
     "refrigerator",  "book",         "clock",
     "vase",          "scissors",     "teddy bear",
     "hair drier",    "toothbrush"}};

int PTQEfficientDetPostProcessMethod::Init(
    const std::string &config_file_path) {
  VLOG(EXAMPLE_DEBUG) << "PTQEfficientDetPostProcessMethod init from file";
  std::ifstream ifs(config_file_path.c_str());
  if (!ifs) {
    VLOG(EXAMPLE_SYSTEM) << "Open config file " << config_file_path
                         << " failed";
    return -1;
  }

  std::stringstream buffer;
  buffer << ifs.rdbuf();
  std::string contents(buffer.str());
  return this->InitFromJsonString(contents);
}

int PTQEfficientDetPostProcessMethod::InitFromJsonString(
    const std::string &config) {
  VLOG(EXAMPLE_DEBUG) << "PTQEfficientDetPostProcessMethod Json string:"
                      << config.data();

  char *latency_log = getenv("SHOW_LATENCY_LOG");
  latency_status_ = (latency_log != nullptr) ? atoi(latency_log) : 0;

  rapidjson::Document document;
  document.Parse(config.data());

  if (document.HasMember("score_threshold")) {
    score_threshold_ = document["score_threshold"].GetFloat();
    // Align with mapper
    score_threshold_ = -std::log(1 / score_threshold_ - 1);
  }

  if (document.HasMember("nms_threshold")) {
    nms_threshold_ = document["nms_threshold"].GetFloat();
  }

  if (document.HasMember("dequanti_file")) {
    dequanti_file_ = document["dequanti_file"].GetString();
    // resize vector size
    efficient_det_config_.scales.resize(10);
    for (int i = 0; i < 5; i++) {
      efficient_det_config_.scales[i].resize(720);
      efficient_det_config_.scales[i + 5].resize(36);
    }
    // read scales for tensors
    std::fstream infile;
    infile.open(dequanti_file_.c_str(), std::ios_base::in);
    if (!infile.is_open()) {
      VLOG(EXAMPLE_SYSTEM) << "Open file " << dequanti_file_.c_str()
                           << " failed!";
      return -1;
    }
    for (int i = 0; i < 10; i++) {
      for (int j = 0; j < efficient_det_config_.scales[i].size(); j++) {
        infile >> efficient_det_config_.scales[i][j];
      }
    }
    this->has_dequanti_node_ = false;
  }
  return 0;
}

std::vector<xstream::BaseDataPtr> PTQEfficientDetPostProcessMethod::DoProcess(
    const std::vector<xstream::BaseDataPtr> &input,
    const xstream::InputParamPtr &param) {
  auto image_tensor = std::static_pointer_cast<ImageTensor>(input[0]);
  auto output_tensor = std::static_pointer_cast<TensorVector>(input[1]);
  auto perception = std::shared_ptr<Perception>(new Perception);

  static thread_local Latency latency;
  if (latency_status_) SetLatency(latency);
  //  PostProcess
  this->PostProcess(
      output_tensor->tensors, image_tensor.get(), perception.get());
  if (latency_status_) UpdateLatency(latency);

  VLOG(EXAMPLE_DETAIL) << "PostProcess success!";
  release_output_tensor(output_tensor->tensors);
  VLOG(EXAMPLE_DETAIL) << "release output tensor success!";

  std::vector<xstream::BaseDataPtr> res;
  perception->name_ = output_name_;
  res.emplace_back(xstream::BaseDataPtr(perception));
  VLOG(EXAMPLE_DETAIL) << "PTQEfficientDetPostProcessMethod DoProcess "
                          "finished, predict result: "
                       << *(perception.get());
  return res;
}

void PTQEfficientDetPostProcessMethod::Finalize() {}

int PTQEfficientDetPostProcessMethod::UpdateParameter(
    xstream::InputParamPtr ptr) {
  VLOG(EXAMPLE_REPORT) << "UpdateParameter in PTQEfficientDetPostProcessMethod "
                          "not supported yet.";
  return 0;
}

xstream::InputParamPtr PTQEfficientDetPostProcessMethod::GetParameter() const {
  VLOG(EXAMPLE_DETAIL) << "PTQEfficientDetPostProcessMethod GetParameter";
  xstream::InputParamPtr ptr;
  return ptr;
}

std::string PTQEfficientDetPostProcessMethod::GetVersion() const {}

xstream::MethodInfo PTQEfficientDetPostProcessMethod::GetMethodInfo() {
  VLOG(EXAMPLE_DETAIL) << "PTQEfficientDetPostProcessMethod GetMethodInfo";
  xstream::MethodInfo order_method = xstream::MethodInfo();
  // TODO(ruxin.song): need to change to thread safety later
  order_method.is_thread_safe_ = false;
  order_method.is_need_reorder_ = false;
  order_method.is_src_ctx_dept_ = false;
  return order_method;
}

PTQEfficientDetPostProcessMethod::~PTQEfficientDetPostProcessMethod() = default;

int PTQEfficientDetPostProcessMethod::GetAnchors(std::vector<EDAnchor> &anchors,
                                                 int layer,
                                                 int feat_height,
                                                 int feat_width) {
  int stride = default_efficient_det_config.feature_strides[layer];
  auto scales = default_efficient_det_config.anchor_scales[layer];
  const auto &ratios = default_efficient_det_config.anchor_ratio;
  int w = stride, h = stride;
  int size = w * h;
  float x_ctr = 0.5 * (w - 1.f);
  float y_ctr = 0.5 * (h - 1.f);
  // base anchor
  std::vector<EDBaseAnchor> base_anchors;
  for (const auto &ratio : ratios) {
    for (const auto &scale : scales) {
      double size_ratio = std::floor(size / ratio);
      double new_w = std::floor(std::sqrt(size_ratio) + 0.5) * scale;
      double new_h = std::floor(new_w / scale * ratio + 0.5) * scale;
      base_anchors.push_back(EDBaseAnchor(x_ctr - 0.5f * (new_w - 1.f),
                                          y_ctr - 0.5f * (new_h - 1.f),
                                          x_ctr + 0.5f * (new_w - 1.f),
                                          y_ctr + 0.5f * (new_h - 1.f)));
    }
  }

  for (int i = 0; i < feat_height; ++i) {
    for (int j = 0; j < feat_width; ++j) {
      auto ori_y = i * stride;
      auto ori_x = j * stride;
      for (const auto &base_anchor : base_anchors) {
        float x1 = base_anchor.x1_ + ori_x;
        float y1 = base_anchor.y1_ + ori_y;
        float x2 = base_anchor.x2_ + ori_x;
        float y2 = base_anchor.y2_ + ori_y;
        float width = x2 - x1 + 1.f;
        float height = y2 - y1 + 1.f;
        float ctr_x = x1 + 0.5f * (width - 1.f);
        float ctr_y = y1 + 0.5f * (height - 1.f);

        anchors.push_back(EDAnchor(ctr_x, ctr_y, width, height));
      }
    }
  }
}

static inline uint32x4x4_t CalculateIndex(uint32_t idx,
                                          float32x4_t a,
                                          float32x4_t b,
                                          uint32x4x4_t c) {
  uint32x4_t mask{0};
  mask = vcltq_f32(b, a);
  uint32x4_t vec_idx = {idx, idx + 1, idx + 2, idx + 3};
  uint32x4x4_t res = {{vbslq_u32(mask, vec_idx, c.val[0]), 0, 0, 0}};
  return res;
}

static inline float32x2_t CalculateMax(float32x4_t in) {
  auto pmax = vpmax_f32(vget_high_f32(in), vget_low_f32(in));
  return vpmax_f32(pmax, pmax);
}

static inline uint32_t CalculateVectorIndex(uint32x4x4_t vec_res_idx,
                                            float32x4_t vec_res_value) {
  uint32x4_t res_idx_mask{0};
  uint32x4_t mask_ones = vdupq_n_u32(0xFFFFFFFF);

  auto pmax = CalculateMax(vec_res_value);
  auto mask = vceqq_f32(vec_res_value, vcombine_f32(pmax, pmax));
  res_idx_mask = vandq_u32(vec_res_idx.val[0], mask);
  res_idx_mask = vaddq_u32(res_idx_mask, mask_ones);
  auto pmin =
      vpmin_u32(vget_high_u32(res_idx_mask), vget_low_u32(res_idx_mask));
  pmin = vpmin_u32(pmin, pmin);
  uint32_t res = vget_lane_u32(pmin, 0);
  return (res - 0xFFFFFFFF);
}

static std::pair<float, int> MaxScoreID(int32_t *input,
                                        float *scale,
                                        int length) {
  float init_res_value = input[0] * scale[0];
  float32x4_t vec_res_value = vdupq_n_f32(init_res_value);
  uint32x4x4_t vec_res_idx{{0}};
  int i = 0;
  for (; i <= (length - 4); i += 4) {
    int32x4_t vec_input = vld1q_s32(input + i);
    float32x4_t vec_scale = vld1q_f32(scale + i);

    float32x4_t vec_elements = vmulq_f32(vcvtq_f32_s32(vec_input), vec_scale);
    float32x4_t temp_vec_res_value = vmaxq_f32(vec_elements, vec_res_value);
    vec_res_idx =
        CalculateIndex(i, temp_vec_res_value, vec_res_value, vec_res_idx);
    vec_res_value = temp_vec_res_value;
  }

  uint32_t idx = CalculateVectorIndex(vec_res_idx, vec_res_value);
  float res = vget_lane_f32(CalculateMax(vec_res_value), 0);

  // Compute left elements
  for (; i < length; ++i) {
    float score = input[i] * scale[i];
    if (score > res) {
      idx = i;
      res = score;
    }
  }
  std::pair<float, int> result_id_score = {res, idx};
  return result_id_score;
}

int PTQEfficientDetPostProcessMethod::GetBboxAndScores(
    hbDNNTensor *c_tensor,
    hbDNNTensor *bbox_tensor,
    std::vector<Detection> &dets,
    std::vector<EDAnchor> &anchors,
    int class_num,
    float img_h,
    float img_w,
    int tensor_index) {
  auto *shape = c_tensor->properties.validShape.dimensionSize;
  int32_t c_batch_size = shape[0];
  int h_idx, w_idx, c_idx;
  get_tensor_hwc_index(c_tensor, &h_idx, &w_idx, &c_idx);

  int32_t c_hnum = shape[h_idx];
  int32_t c_wnum = shape[w_idx];
  int32_t c_cnum = shape[c_idx];
  uint32_t anchor_num_per_pixel = c_cnum / class_num;

  shape = bbox_tensor->properties.validShape.dimensionSize;
  int32_t b_batch_size = shape[0];
  get_tensor_hwc_index(bbox_tensor, &h_idx, &w_idx, &c_idx);
  int32_t b_hnum = shape[h_idx];
  int32_t b_wnum = shape[w_idx];
  int32_t b_cnum = shape[c_idx];

  assert(anchor_num_per_pixel == b_cnum / 4);
  assert(c_batch_size == b_batch_size && c_hnum == b_hnum && c_wnum == b_wnum);
  auto box_num = b_batch_size * b_hnum * b_wnum * anchor_num_per_pixel;

  hbSysFlushMem(&(c_tensor->sysMem[0]), HB_SYS_MEM_CACHE_INVALIDATE);
  hbSysFlushMem(&(bbox_tensor->sysMem[0]), HB_SYS_MEM_CACHE_INVALIDATE);

  if (this->has_dequanti_node_) {
    auto *raw_cls_data = reinterpret_cast<float *>(c_tensor->sysMem[0].virAddr);
    auto *raw_box_data =
        reinterpret_cast<float *>(bbox_tensor->sysMem[0].virAddr);
    for (int i = 0; i < box_num; i++) {
      uint32_t res_id_cur_anchor = i * class_num;
      int max_id = 0;
      float max_score = raw_cls_data[res_id_cur_anchor];
      if (class_num % 4 != 0) {
        for (int cls = 1; cls < class_num; ++cls) {
          float cls_value = raw_cls_data[res_id_cur_anchor + cls];
          if (cls_value > max_score) {
            max_score = cls_value;
            max_id = cls;
          }
        }
      } else {
        int max_id_0 = 0;
        int max_id_1 = 1;
        int max_id_2 = 2;
        int max_id_3 = 3;
        float max_score_0 = raw_cls_data[res_id_cur_anchor];
        float max_score_1 = raw_cls_data[res_id_cur_anchor + 1];
        float max_score_2 = raw_cls_data[res_id_cur_anchor + 2];
        float max_score_3 = raw_cls_data[res_id_cur_anchor + 3];
        for (int cls = 0; cls < class_num; cls += 4) {
          float cls_value_0 = raw_cls_data[res_id_cur_anchor + cls];
          float cls_value_1 = raw_cls_data[res_id_cur_anchor + cls + 1];
          float cls_value_2 = raw_cls_data[res_id_cur_anchor + cls + 2];
          float cls_value_3 = raw_cls_data[res_id_cur_anchor + cls + 3];
          if (cls_value_0 > max_score_0) {
            max_score_0 = cls_value_0;
            max_id_0 = cls;
          }

          if (cls_value_1 > max_score_1) {
            max_score_1 = cls_value_1;
            max_id_1 = cls + 1;
          }

          if (cls_value_2 > max_score_2) {
            max_score_2 = cls_value_2;
            max_id_2 = cls + 2;
          }

          if (cls_value_3 > max_score_3) {
            max_score_3 = cls_value_3;
            max_id_3 = cls + 3;
          }
        }

        if (max_score_0 > max_score) {
          max_score = max_score_0;
          max_id = max_id_0;
        }

        if (max_score_1 > max_score) {
          max_score = max_score_1;
          max_id = max_id_1;
        }

        if (max_score_2 > max_score) {
          max_score = max_score_2;
          max_id = max_id_2;
        }

        if (max_score_3 > max_score) {
          max_score = max_score_3;
          max_id = max_id_3;
        }
      }
      // box
      if (max_score <= score_threshold_) continue;

      // aligned index
      int start = i * 4;
      float dx = raw_box_data[start];
      float dy = raw_box_data[start + 1];
      float dw = raw_box_data[start + 2];
      float dh = raw_box_data[start + 3];
      float width = anchors[i].w_;
      float height = anchors[i].h_;
      float ctr_x = anchors[i].c_x_;
      float ctr_y = anchors[i].c_y_;

      float pred_ctr_x = dx * width + ctr_x;
      float pred_ctr_y = dy * height + ctr_y;
      float pred_w = std::exp(dw) * width;
      float pred_h = std::exp(dh) * height;

      // python在这里需要对框做clip,  x >= 0 && x <= input_width...
      float xmin = (pred_ctr_x - 0.5f * (pred_w - 1.f));
      float ymin = (pred_ctr_y - 0.5f * (pred_h - 1.f));
      float xmax = (pred_ctr_x + 0.5f * (pred_w - 1.f));
      float ymax = (pred_ctr_y + 0.5f * (pred_h - 1.f));

      Bbox bbox;
      bbox.xmin = std::max(xmin, 0.f);
      bbox.ymin = std::max(ymin, 0.f);
      bbox.xmax = std::min(xmax, img_w);
      bbox.ymax = std::min(ymax, img_h);

      dets.push_back(
          Detection(max_id,
                    max_score,
                    bbox,
                    efficient_det_config_.class_names[max_id].c_str()));
    }
  } else {
    auto *raw_cls_data =
        reinterpret_cast<int32_t *>(c_tensor->sysMem[0].virAddr);
    auto *raw_box_data =
        reinterpret_cast<int32_t *>(bbox_tensor->sysMem[0].virAddr);
    for (int i = 0; i < box_num; i++) {
      // score and cls
      uint32_t res_id_cur_anchor = i * class_num;
      int cls_scale_index = res_id_cur_anchor % 720;
      // cls scales for every box
      auto cls_scales =
          &efficient_det_config_.scales[tensor_index][cls_scale_index];
      // 获取max_id and max_score;
      auto max_score_id =
          MaxScoreID(raw_cls_data + res_id_cur_anchor, cls_scales, class_num);
      float max_score = max_score_id.first;
      int max_id = max_score_id.second;

      // box
      if (max_score <= score_threshold_) continue;

      // stride is 40, if do not have dequanti node
      int start = i * 4 + (4 * i) / 36 * 4;
      int scale_index = (i * 4) % 36;
      // box scales for every box
      auto box_scales =
          &efficient_det_config_.scales[tensor_index + 5][scale_index];
      float dx = raw_box_data[start] * box_scales[0];
      float dy = raw_box_data[start + 1] * box_scales[1];
      float dw = raw_box_data[start + 2] * box_scales[2];
      float dh = raw_box_data[start + 3] * box_scales[3];
      float width = anchors[i].w_;
      float height = anchors[i].h_;
      float ctr_x = anchors[i].c_x_;
      float ctr_y = anchors[i].c_y_;

      float pred_ctr_x = dx * width + ctr_x;
      float pred_ctr_y = dy * height + ctr_y;
      float pred_w = std::exp(dw) * width;
      float pred_h = std::exp(dh) * height;

      // python在这里需要对框做clip,  x >= 0 && x <= input_width...
      float xmin = (pred_ctr_x - 0.5f * (pred_w - 1.f));
      float ymin = (pred_ctr_y - 0.5f * (pred_h - 1.f));
      float xmax = (pred_ctr_x + 0.5f * (pred_w - 1.f));
      float ymax = (pred_ctr_y + 0.5f * (pred_h - 1.f));

      Bbox bbox;
      bbox.xmin = std::max(xmin, 0.f);
      bbox.ymin = std::max(ymin, 0.f);
      bbox.xmax = std::min(xmax, img_w);
      bbox.ymax = std::min(ymax, img_h);

      dets.push_back(
          Detection(max_id,
                    max_score,
                    bbox,
                    efficient_det_config_.class_names[max_id].c_str()));
    }
  }
  return 0;
}

int PTQEfficientDetPostProcessMethod::PostProcess(
    std::vector<hbDNNTensor> &tensors,
    ImageTensor *image_tensor,
    Perception *perception) {
  perception->type = Perception::DET;
  // TODO(yaoyao.sun) 去掉这个判断，取前半部分做分类，后半部分做框
  float origin_height = image_tensor->ori_image_height;
  float origin_width = image_tensor->ori_image_width;

  auto input_height = image_tensor->height();
  auto input_width = image_tensor->width();

  float w_ratio = input_width * 1.f / origin_width;
  float h_ratio = input_height * 1.f / origin_height;
  float scale = std::min(w_ratio, h_ratio);
  float new_h = input_height;
  float new_w = input_width;
  if (image_tensor->is_pad_resize) {
    new_h = scale * origin_height;
    new_w = scale * origin_width;
    new_h = std::ceil(new_h / 128.f) * 128.f;
    new_w = std::ceil(new_w / 128.f) * 128.f;
    w_ratio = scale;
    h_ratio = scale;
  }

  int layer_num = efficient_det_config_.feature_strides.size();
  if (!anchor_init_) {
    anchors_table_.resize(layer_num);

    for (int i = 0; i < layer_num; i++) {
      int height, width;
      get_tensor_aligned_hw(tensors[i], &height, &width);
      GetAnchors(anchors_table_[i], i, height, width);
    }
    anchor_init_ = true;
  }

  std::vector<Detection> dets;

  for (int i = 0; i < layer_num; i++) {
    std::vector<EDAnchor> &anchors = anchors_table_[i];
    int anchors_num = anchors.size();
    GetBboxAndScores(&tensors[i],
                     &tensors[layer_num + i],
                     dets,
                     anchors,
                     kEfficientDetClassNum,
                     new_h,
                     new_w,
                     i);
  }
  yolo5_nms(dets, nms_threshold_, 6000, perception->det, false);
  if (perception->det.size() > nms_top_k_) {
    perception->det.resize(nms_top_k_);
  }

  for (auto &box : perception->det) {
    box.bbox.xmax /= w_ratio;
    box.bbox.xmin /= w_ratio;
    box.bbox.ymax /= h_ratio;
    box.bbox.ymin /= h_ratio;

    //    box.bbox.xmin = std::max(box.bbox.xmin, 0.f);
    //    box.bbox.ymin = std::max(box.bbox.ymin, 0.f);
    box.bbox.xmax = std::min(box.bbox.xmax, origin_width - 1) + 1;
    box.bbox.ymax = std::min(box.bbox.ymax, origin_height - 1) + 1;
  }
  return 0;
}

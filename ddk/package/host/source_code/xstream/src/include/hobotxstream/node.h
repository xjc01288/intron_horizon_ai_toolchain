/**
 * Copyright (c) 2018 Horizon Robotics. All rights reserved.
 * @brief     Node in xstream framework
 * @file      node.h
 * @author    shuhuan.sun
 * @email     shuhuan.sun@horizon.ai
 * @version   0.0.0.1
 * @date      2018.11.21
 */
#ifndef HOBOTXSTREAM_NODE_H_
#define HOBOTXSTREAM_NODE_H_

#include <chrono>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "common/concurrency_queue.h"
#include "common/xthread.h"
#include "common/thread_manager.h"
#include "hobotxstream/framework_data_shell.h"
#include "xstream/method.h"
#include "hobotxstream/method_manager.h"
#include "hobotxstream/xstream_config.h"

namespace xstream {

class Sponge {
 public:
  explicit Sponge(int16_t max_size = 128) : cache_max_size_(max_size) {
    // last_proc_timestamp_ = 0;
    is_start_ = false;
  }

  ~Sponge() = default;

  bool Sop(const FrameworkDataPtr &data, std::list<FrameworkDataPtr> *ready);

 private:
  bool is_start_;
  uint16_t cache_max_size_;
  // int32_t waiting_time_;
  // int64_t last_proc_timestamp_;
  uint64_t expected_sequence_id_;
  std::list<FrameworkDataPtr> cache_list_;
  // std::mutex cache_mutex_;
};

struct NodeRunContext {
 private:
  XThreadRawPtr daemon_thread_;
  ThreadManager *engine_;
  XStreamSharedConfig sharedconfig_;
  ProfilerPtr profiler_;

 public:
  NodeRunContext(XThreadRawPtr daemon_thread, ThreadManager *engine,
                 XStreamSharedConfig sharedconfig, ProfilerPtr profiler)
      : daemon_thread_(daemon_thread),
        engine_(engine),
        sharedconfig_(sharedconfig),
        profiler_(profiler) {}
  ThreadManager *GetEngine() const { return engine_; }
  XThreadRawPtr GetNodeDaemon() const { return daemon_thread_; }
  XStreamSharedConfig GetSharedConfig() const { return sharedconfig_; }
  ProfilerPtr GetProfiler() const { return profiler_; }
};

class Node : public std::enable_shared_from_this<Node> {
 public:
  explicit Node(const std::string &unique_name) : unique_name_(unique_name) {}
  ~Node() = default;

  void Init(
      std::function<int(FrameworkDataPtr data, std::shared_ptr<Node> readyNode)>
          callback,
      const std::vector<int> &inputSlots, const std::vector<int> &outputSlots,
      const Config &config, std::shared_ptr<NodeRunContext> run_context);

  /// 此函数为scheduler调用接口函数，用于驱动Method计算
  void Do(const FrameworkDataPtr &data);

  int SetParameter(InputParamPtr ptr);

  InputParamPtr GetParameter() const;

  std::string GetVersion() const;

  std::string GetUniqueName() const;

 private:
  ProfilerPtr profiler_;
  // node唯一名字，Note:"__INPUT__"内部预留使用了。
  std::string unique_name_;
  MethodManager method_manager_;
  // call_back func, be called after Node complete process
  std::function<int(FrameworkDataPtr data, std::shared_ptr<Node> ready_node)>
      on_ready_;
  std::vector<int> input_slots_, output_slots_;

  bool is_need_reorder_;

  std::vector<Sponge> sponge_list_;

  int32_t setting_timeout_duration_ms_ = 0;  // milliseconds
  XThreadRawPtr daemon_thread_ = nullptr;

 protected:
  void Handle(const FrameworkDataPtr &framework_data);
  //
  bool IsNeedSkip(const FrameworkDataPtr &framework_data);
  // Node is disabled, get 'Fake' output based on DisableParam::Mode
  void FakeResult(const FrameworkDataPtr &framework_data);
  // 供timer以及Method的回调函数使用，把消息发给Node线程以便把结果写回并推送给scheduler
  void PostResult(FrameworkDataShellPtr result);

  std::vector<InputParamPtr> GetInputParams(
      const FrameworkDataBatchPtr &frame_data) const;

  std::vector<std::vector<BaseDataPtr>> GetInputData(
      const FrameworkDataBatchPtr &data) const;

  // set FrameworkDataBatch.datas_ as data (normal)
  void SetOutputData(FrameworkDataBatchPtr frameData,
                     const std::vector<std::vector<BaseDataPtr>> &data);

  // // set FrameworkDataBatch.datas_ as Time out
  // void SetOutputDataTimeout(FrameworkDataBatchPtr frameData);

  // void CheckResult(const FrameworkData &frame_data);

  // Node is disabled, schedule next node
  void OnFakeResult(FrameworkDataPtr result);

  // set FrameworkDataShell data, and schedule next node
  void OnGetResult(FrameworkDataShellPtr result);
};

typedef std::shared_ptr<Node> NodePtr;

#define NODE_UNINAME_RESERVED_INPUT "__INPUT__"
}  // namespace xstream

#endif  // HOBOTXSTREAM_NODE_H_

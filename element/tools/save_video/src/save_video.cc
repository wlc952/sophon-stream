//===----------------------------------------------------------------------===//
//
// Copyright (C) 2025 Sophgo Technologies Inc.  All rights reserved.
//
// SOPHON-STREAM is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#include "save_video.h"

#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>

#include "common/common_defs.h"
#include "common/logger.h"
#include "element_factory.h"

namespace fs = std::filesystem;

namespace sophon_stream {
namespace element {
namespace save_video {

using namespace std::chrono;

SaveVideo::SaveVideo() {}
SaveVideo::~SaveVideo() {
  cleanup_running_ = false;
  if (cleanup_thread_.joinable()) cleanup_thread_.join();
}

static std::string now_datetime_str() {
  auto now = system_clock::now();
  std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return std::string(buf);
}

std::optional<ServerEndpoint> SaveVideo::parseUrl(const std::string& url) {
  // scheme://host:port/path
  std::regex re(R"((http|https)://([^/:]+)(?::(\d+))?(/.*))");
  std::smatch m;
  if (!std::regex_match(url, m, re)) return std::nullopt;
  ServerEndpoint ep;
  ep.scheme = m[1].str();
  ep.host = m[2].str();
  ep.port = m[3].matched ? std::stoi(m[3].str()) : (ep.scheme == "https" ? 443 : 80);
  ep.path = m[4].str();
  return ep;
}

bool SaveVideo::saveSnapshot(const common::Frame& frame, const std::string& filepath) {
  try {
    fs::create_directories(fs::path(filepath).parent_path());
    cv::Mat img;
    // 优先使用已绘制 OSD 的图像
    if (frame.mSpDataOsd) {
      cv::bmcv::toMAT(frame.mSpDataOsd.get(), img, true);
    } else if (frame.mSpData) {
      cv::bmcv::toMAT(frame.mSpData.get(), img, true);
    } else if (!frame.mMat.empty()) {
      img = frame.mMat;
    } else {
      return false;
    }
    return cv::imwrite(filepath, img);
  } catch (const std::exception& e) {
    IVS_ERROR("saveSnapshot error: {0}", e.what());
    return false;
  }
}

bool SaveVideo::startRecording(int channel, const common::Frame& frame, const std::string& filepath) {
  auto& st = channels_[channel];
  try {
    fs::create_directories(fs::path(filepath).parent_path());
    int fps = st.fps > 0 ? st.fps : (frame.mFrameRate.mDenominator != 0 ? std::max(1, frame.mFrameRate.mNumber / std::max(1, frame.mFrameRate.mDenominator)) : 25);
    int w = frame.mWidth;
    int h = frame.mHeight;
    if (w <= 0 || h <= 0) return false;
    st.width = w;
    st.height = h;
    st.fps = fps;
    st.pre_buf_max = std::max(0, pre_record_seconds_ * fps);

    st.writer = std::make_unique<cv::VideoWriter>();
  // MP4 容器更推荐使用 'avc1' FourCC，避免 OpenCV/FFmpeg 警告并保持 H.264 编码
  int fourcc = cv::VideoWriter::fourcc('a', 'v', 'c', '1');
    bool ok = st.writer->open(filepath, fourcc, fps, cv::Size(w, h));
    if (!ok) {
      IVS_ERROR("open VideoWriter failed: {0}", filepath);
      st.writer.reset();
      return false;
    }
    st.recording = true;
    st.record_end_tp = steady_clock::now() + seconds(record_seconds_);
    return true;
  } catch (const std::exception& e) {
    IVS_ERROR("startRecording error: {0}", e.what());
    return false;
  }
}

void SaveVideo::appendFrame(int channel, const common::Frame& frame) {
  auto it = channels_.find(channel);
  if (it == channels_.end()) return;
  auto& st = it->second;
  if (!st.recording || !st.writer) return;
  cv::Mat img;
  // 优先写入 OSD 后的图像
  if (frame.mSpDataOsd) {
    cv::bmcv::toMAT(frame.mSpDataOsd.get(), img, true);
  } else if (frame.mSpData) {
    cv::bmcv::toMAT(frame.mSpData.get(), img, true);
  } else if (!frame.mMat.empty()) {
    img = frame.mMat;
  } else {
    return;
  }
  if (img.cols != st.width || img.rows != st.height) {
    cv::Mat resized;
    cv::resize(img, resized, cv::Size(st.width, st.height));
    st.writer->write(resized);
  } else {
    st.writer->write(img);
  }
}

void SaveVideo::stopRecording(int channel) {
  auto it = channels_.find(channel);
  if (it == channels_.end()) return;
  auto& st = it->second;
  if (st.writer) {
    st.writer->release();
  }
  st.writer.reset();
  st.recording = false;
}

std::string SaveVideo::makeUrl(const std::string& file_abs_path) const {
  if (base_file_url_.empty()) return file_abs_path;  // 直接返回本地路径
  // 将 save_dir_ 作为根去掉前缀
  std::string rel = file_abs_path;
  if (!save_dir_.empty()) {
    try {
      fs::path relp = fs::relative(file_abs_path, save_dir_);
      rel = relp.generic_string();
    } catch (...) {
    }
  }
  if (base_file_url_.back() == '/') return base_file_url_ + rel;
  return base_file_url_ + "/" + rel;
}

void SaveVideo::postAlarm(int channel, const std::string& imgPath, const std::string& videoPath, const std::string& datetime_str, int resolved_type) {
  if (!endpoint_.has_value()) return;
  const auto& ep = endpoint_.value();
  try {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    httplib::Client cli(ep.scheme + "://" + ep.host + ":" + std::to_string(ep.port));
#else
    httplib::Client cli(ep.host, ep.port);
#endif
    nlohmann::json j;
    j["deviceId"] = device_id_;
    j["deviceIp"] = device_ip_;
    j["safetyId"] = safety_id_;
    j["safetyName"] = safety_name_;
    j["warning"] = warning_;
    j["type"] = resolved_type;
    if (video_url_field_ == "safetyUrl") {
      j["safetyUrl"] = makeUrl(videoPath);
      j["brakeUrl"] = "";
    } else {
      j["brakeUrl"] = makeUrl(videoPath);
      j["safetyUrl"] = "";
    }
    j["datatime"] = datetime_str;
    j["imgUrl"] = makeUrl(imgPath);
    auto res = cli.Post(ep.path.c_str(), j.dump(), "application/json");
    if (!res) {
      IVS_ERROR("save_video http post failed: no response");
    } else if (res->status != 200) {
      IVS_ERROR("save_video http status: {0}", res->status);
    } else {
      IVS_INFO("save_video http ok: status=200, len={0}", res->body.size());
    }
  } catch (const std::exception& e) {
    IVS_ERROR("save_video http exception: {0}", e.what());
  }
}

common::ErrorCode SaveVideo::initInternal(const std::string& json) {
  auto cfg = nlohmann::json::parse(json, nullptr, false);
  if (!cfg.is_object()) return common::ErrorCode::PARSE_CONFIGURE_FAIL;

  // 处理嵌套的 "configure" 对象
  if (cfg.contains("configure") && cfg["configure"].is_object()) {
    cfg = cfg["configure"];  // 直接使用内层对象
    IVS_INFO("save_video: using nested 'configure' object");
  }

  // 基础配置
  if (cfg.contains(CONFIG_SERVER_URL)) server_url_ = cfg[CONFIG_SERVER_URL].get<std::string>();
  endpoint_ = parseUrl(server_url_);
  if (!cfg.contains(CONFIG_SAVE_DIR)) return common::ErrorCode::PARSE_CONFIGURE_FAIL;
  save_dir_ = cfg[CONFIG_SAVE_DIR].get<std::string>();
  if (cfg.contains(CONFIG_BASE_FILE_URL)) base_file_url_ = cfg[CONFIG_BASE_FILE_URL].get<std::string>();
  if (cfg.contains(CONFIG_RECORD_SECONDS)) record_seconds_ = std::max(1, cfg[CONFIG_RECORD_SECONDS].get<int>());
  if (cfg.contains(CONFIG_PRE_RECORD_SECONDS)) pre_record_seconds_ = std::max(0, cfg[CONFIG_PRE_RECORD_SECONDS].get<int>());
  if (cfg.contains(CONFIG_COOLDOWN_SECONDS)) cooldown_seconds_ = std::max(0, cfg[CONFIG_COOLDOWN_SECONDS].get<int>());
  if (cfg.contains(CONFIG_TRIGGER_CLASSES)) trigger_classes_ = cfg[CONFIG_TRIGGER_CLASSES].get<std::vector<std::string>>();

  // 存储清理
  if (cfg.contains(CONFIG_RETENTION_DAYS)) retention_days_ = std::max(0, cfg[CONFIG_RETENTION_DAYS].get<int>());
  if (cfg.contains(CONFIG_RETENTION_MAX_GB)) retention_max_gb_ = std::max(0.0, cfg[CONFIG_RETENTION_MAX_GB].get<double>());
  if (cfg.contains(CONFIG_CLEANUP_INTERVAL_SECONDS)) cleanup_interval_seconds_ = std::max(30, cfg[CONFIG_CLEANUP_INTERVAL_SECONDS].get<int>());

  // 协议字段
  if (cfg.contains(CONFIG_DEVICE_ID)) device_id_ = cfg[CONFIG_DEVICE_ID].get<std::string>();
  if (cfg.contains(CONFIG_DEVICE_IP)) device_ip_ = cfg[CONFIG_DEVICE_IP].get<std::string>();
  if (cfg.contains(CONFIG_SAFETY_ID)) safety_id_ = cfg[CONFIG_SAFETY_ID].get<std::string>();
  if (cfg.contains(CONFIG_SAFETY_NAME)) safety_name_ = cfg[CONFIG_SAFETY_NAME].get<std::string>();
  if (cfg.contains(CONFIG_WARNING)) warning_ = cfg[CONFIG_WARNING].get<std::string>();
  if (cfg.contains(CONFIG_TYPE)) {
    const auto& tv = cfg[CONFIG_TYPE];
    if (tv.is_number_integer()) {
      fixed_type_ = tv.get<int>();
      use_class_id_type_ = false;
      type_map_.clear();
      IVS_INFO("save_video: configured fixed_type = {0}", *fixed_type_);
    } else if (tv.is_object()) {
      // 解析 class_id -> type 的映射
      fixed_type_.reset();
      use_class_id_type_ = false;
      type_map_.clear();
      for (auto it = tv.begin(); it != tv.end(); ++it) {
        try {
          int class_id = std::stoi(it.key());
          if (it.value().is_number_integer()) {
            type_map_[class_id] = it.value().get<int>();
          }
        } catch (...) {
          // 非法键名忽略
        }
      }
      std::stringstream ss;
      ss << "save_video: type_map loaded {";
      bool first = true;
      for (auto &kv : type_map_) {
        if (!first) ss << ", ";
        first = false;
        ss << kv.first << "->" << kv.second;
      }
      ss << "}";
      IVS_INFO("{0}", ss.str());
    } else if (tv.is_string()) {
      std::string s = tv.get<std::string>();
      if (s == "class_id" || s == "classid" || s == "label" ) {
        // 显式指明使用检测到的第一个目标 class_id 作为 type
        fixed_type_.reset();
        type_map_.clear();
        use_class_id_type_ = true;
        IVS_INFO("save_video: configured use_class_id_type = true");
      }
    } else {
      IVS_WARN("save_video: unsupported type field format");
    }
  } else {
    IVS_INFO("save_video: no type field found, will use default class_id");
  }
  if (cfg.contains(CONFIG_VIDEO_URL_FIELD)) {
    video_url_field_ = cfg[CONFIG_VIDEO_URL_FIELD].get<std::string>();
    if (video_url_field_ != "safetyUrl" && video_url_field_ != "brakeUrl") {
      video_url_field_ = "safetyUrl";
    }
  }

  try { fs::create_directories(save_dir_); } catch (...) {}
  // 启动清理线程（若配置了任一条件）
  if ((retention_days_ > 0 || retention_max_gb_ > 0.0) && !save_dir_.empty()) {
    cleanup_running_ = true;
    cleanup_thread_ = std::thread(&SaveVideo::cleanupLoop, this);
  }
  return common::ErrorCode::SUCCESS;
}

common::ErrorCode SaveVideo::doWork(int dataPipeId) {
  std::vector<int> inputPorts = getInputPorts();
  int inputPort = inputPorts[0];
  // sink 元素也需要向引擎上报数据：按照约定，sink 使用 outputPort=0
  int outputPort = getSinkElementFlag() ? 0 : getOutputPorts()[0];

  auto data = popInputData(inputPort, dataPipeId);
  while (!data && (getThreadStatus() == ThreadStatus::RUN)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    data = popInputData(inputPort, dataPipeId);
  }
  if (data == nullptr) return common::ErrorCode::SUCCESS;

  auto obj = std::static_pointer_cast<common::ObjectMetadata>(data);
  if (!obj->mFrame || obj->mFrame->mEndOfStream) {
    // 标记 EOF 帧为过滤，避免被上层统计为有效帧
    if (obj && obj->mFrame && obj->mFrame->mEndOfStream) {
      obj->mFilter = true;
    }
    // 透传（包括 sink）：sink 固定用 outDataPipeId=0
    int outDataPipeId = getSinkElementFlag() ? 0 : 0;
    pushOutputData(outputPort, outDataPipeId, obj);
    return common::ErrorCode::SUCCESS;
  }

  int ch = obj->mFrame->mChannelIdInternal;
  auto& st = channels_[ch];

  // 这是有效帧，确保不被统计逻辑过滤
  obj->mFilter = false;

  // 维护回溯缓存（仅保存 Mat，必要时缩放到将来写入的尺寸）
  {
    cv::Mat cur;
    // 优先缓存 OSD 后的图像，确保预录也带框
    if (obj->mFrame->mSpDataOsd) {
      cv::bmcv::toMAT(obj->mFrame->mSpDataOsd.get(), cur, true);
    } else if (obj->mFrame->mSpData) {
      cv::bmcv::toMAT(obj->mFrame->mSpData.get(), cur, true);
    } else if (!obj->mFrame->mMat.empty()) {
      cur = obj->mFrame->mMat;
    }
    if (!cur.empty()) {
      if (st.width > 0 && st.height > 0 && (cur.cols != st.width || cur.rows != st.height)) {
        cv::Mat resized;
        cv::resize(cur, resized, cv::Size(st.width, st.height));
        st.pre_buffer.emplace_back(std::move(resized));
      } else {
        st.pre_buffer.emplace_back(cur.clone());
      }
      if (st.pre_buf_max > 0) {
        while ((int)st.pre_buffer.size() > st.pre_buf_max) st.pre_buffer.pop_front();
      } else {
        // 没有预录要求则限制一个较小上限，避免无限增长
        const int soft_cap = 50;  // 约2秒@25fps
        while ((int)st.pre_buffer.size() > soft_cap) st.pre_buffer.pop_front();
      }
    }
  }

  // 条件：检测到目标（若 trigger_classes_ 非空，则类别需匹配）
  auto hasTarget = [&]() {
    if (!trigger_classes_.empty()) {
      for (auto& det : obj->mDetectedObjectMetadatas) {
        if (!det) continue;
        std::string name = det->mClassifyName.empty() ? det->mLabelName : det->mClassifyName;
        for (auto& t : trigger_classes_) {
          if (name == t) return true;
        }
      }
      return false;
    } else {
      return !obj->mDetectedObjectMetadatas.empty();
    }
  }();

  auto now_tp = steady_clock::now();
  bool cooldown_ok = st.last_trigger_tp == steady_clock::time_point::min() || (now_tp - st.last_trigger_tp) >= seconds(cooldown_seconds_);

  if (hasTarget && cooldown_ok && !st.recording) {
    // 生成文件名：save_dir/channel_#/YYYYmmdd_HHMMSS
    auto now_sys = system_clock::now();
    std::time_t t = system_clock::to_time_t(now_sys);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    int resolved_type = resolveType(obj->mDetectedObjectMetadatas);
    st.pending_type = resolved_type;

  // 生成包含 type 的文件名
  char timebuf[32];
  std::strftime(timebuf, sizeof(timebuf), "%Y%m%d_%H%M%S", &tm);
  std::string timestr(timebuf);
  std::string base_name = timestr + "_type" + std::to_string(resolved_type);
  std::string base = (fs::path(save_dir_) / ("ch_" + std::to_string(ch)) / base_name).string();
  std::string imgPath = base + ".jpg";
  std::string vidPath = base + ".mp4";

    // 保存快照
    saveSnapshot(*obj->mFrame, imgPath);

    // 开始录像
    if (startRecording(ch, *obj->mFrame, vidPath)) {
      // 先把预录缓存写入
      if (!st.pre_buffer.empty()) {
        for (auto& m : st.pre_buffer) {
          if (st.writer) st.writer->write(m);
        }
      }
    }
    st.pending_img_path = imgPath;
    st.pending_video_path = vidPath;
    st.event_time = now_sys;
    st.report_scheduled = true;  // 等录像结束后发送
    st.last_trigger_tp = now_tp;
  }

  // 录像续写与结束
  if (st.recording) {
    appendFrame(ch, *obj->mFrame);
    if (now_tp >= st.record_end_tp) {
      stopRecording(ch);
      if (st.report_scheduled) {
        // 时间格式
        auto t = system_clock::to_time_t(st.event_time);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  postAlarm(ch, st.pending_img_path, st.pending_video_path, buf, st.pending_type);
        st.report_scheduled = false;
      }
    }
  }

  // 透传下游或上报给引擎（sink 也要 push）
  int outDataPipeId = getSinkElementFlag() ? 0 : (ch % getOutputConnectorCapacity(outputPort));
  auto ec = pushOutputData(outputPort, outDataPipeId, obj);
  if (ec != common::ErrorCode::SUCCESS) {
    IVS_WARN("save_video push downstream failed");
  }

  return common::ErrorCode::SUCCESS;
}

int SaveVideo::resolveType(const std::vector<std::shared_ptr<common::DetectedObjectMetadata>>& dets) const {
  // 调试：打印检测到的所有 class_id 和相关信息
  std::stringstream det_info;
  det_info << "save_video: detected objects count=" << dets.size() << ", details=[";
  bool first = true;
  for (auto &d : dets) {
    if (!d) {
      if (!first) det_info << ",";
      first = false;
      det_info << "null";
      continue;
    }
    if (!first) det_info << ",";
    first = false;
    int classify = d->mClassify;  // 使用 mClassify 而不是 getLabel()
    int label = d->getLabel();    // getLabel() 基于 mTopKLabels
    std::string labelName = d->mLabelName;
    std::string classifyName = d->mClassifyName;
    det_info << "{classify:" << classify << ",label:" << label << ",labelName:\"" << labelName << "\",classifyName:\"" << classifyName << "\"}";
  }
  det_info << "]";
  IVS_INFO("{0}", det_info.str());

  // 1. 固定值优先
  if (fixed_type_.has_value()) {
    IVS_INFO("save_video: using fixed_type = {0}", *fixed_type_);
    return *fixed_type_;
  }
  // 2. 显式使用 class_id
  if (use_class_id_type_) {
    for (auto &d : dets) {
      if (d && d->mClassify >= 0) {
        int result = d->mClassify;
        IVS_INFO("save_video: using class_id_type = {0}", result);
        return result;
      }
    }
    IVS_WARN("save_video: class_id_type mode but no valid detections, fallback to 0");
    return 0; // 无检测则回退 0
  }
  // 3. 映射
  if (!type_map_.empty()) {
    for (auto &d : dets) {
      if (!d) continue;
      int cid = d->mClassify;  // 使用 mClassify 而不是 getLabel()
      if (cid < 0) continue; // 跳过无效的 class_id
      auto it = type_map_.find(cid);
      if (it != type_map_.end()) {
        IVS_INFO("save_video: mapped class_id {0} -> type {1}", cid, it->second);
        return it->second;
      }
    }
    // 未命中映射：若有有效检测对象，用第一个的 class_id
    for (auto &d : dets) {
      if (d && d->mClassify >= 0) {
        int result = d->mClassify;
        IVS_WARN("save_video: mapping miss, fallback to class_id {0}", result);
        return result;
      }
    }
    IVS_WARN("save_video: mapping mode but no valid detections (all mClassify < 0), fallback to 0");
    return 0;
  }
  // 4. 未配置 type：使用第一个有效检测对象 class_id
  for (auto &d : dets) {
    if (d && d->mClassify >= 0) {
      int result = d->mClassify;
      IVS_INFO("save_video: default mode, using class_id {0}", result);
      return result;
    }
  }
  IVS_WARN("save_video: no detections and no fixed type, fallback to 0");
  return 0;
}

REGISTER_WORKER("save_video", SaveVideo)

}  // namespace save_video
}  // namespace element
}  // namespace sophon_stream

// ========== Cleanup loop implementation (placed in global namespace scope) ==========
namespace sophon_stream {
namespace element {
namespace save_video {

void SaveVideo::cleanupLoop() {
  using fs_entry = std::pair<fs::path, std::filesystem::file_time_type>;
  while (cleanup_running_) {
    try {
      // 1) 按天清理
      if (retention_days_ > 0) {
        auto cutoff = std::chrono::system_clock::now() - std::chrono::hours(24 * retention_days_);
        auto cutoff_fs = std::chrono::time_point_cast<std::filesystem::file_time_type::duration>(
            std::filesystem::file_time_type::clock::now() + (cutoff - std::chrono::system_clock::now()));

        for (auto& dir_entry : fs::recursive_directory_iterator(save_dir_)) {
          if (!dir_entry.is_regular_file()) continue;
          auto p = dir_entry.path();
          auto ext = p.extension().string();
          if (!(ext == ".mp4" || ext == ".jpg" || ext == ".jpeg")) continue;
          std::error_code ec;
          auto ft = fs::last_write_time(p, ec);
          if (ec) continue;
          if (ft < cutoff_fs) {
            fs::remove(p, ec);
          }
        }
      }

      // 2) 容量清理（删除最旧文件直到低于阈值）
      if (retention_max_gb_ > 0.0) {
        uintmax_t total_bytes = 0;
        std::vector<fs_entry> files;
        for (auto& dir_entry : fs::recursive_directory_iterator(save_dir_)) {
          if (!dir_entry.is_regular_file()) continue;
          auto p = dir_entry.path();
          auto ext = p.extension().string();
          if (!(ext == ".mp4" || ext == ".jpg" || ext == ".jpeg")) continue;
          std::error_code ec;
          auto sz = fs::file_size(p, ec);
          if (ec) continue;
          total_bytes += sz;
          auto ft = fs::last_write_time(p, ec);
          if (ec) continue;
          files.emplace_back(p, ft);
        }
        const double total_gb = (double)total_bytes / (1024.0 * 1024.0 * 1024.0);
        if (total_gb > retention_max_gb_) {
          std::sort(files.begin(), files.end(), [](const fs_entry& a, const fs_entry& b) {
            return a.second < b.second;  // old first
          });
          std::error_code ec;
          for (auto& f : files) {
            auto sz = fs::file_size(f.first, ec);
            fs::remove(f.first, ec);
            if (!ec && sz > 0) {
              total_bytes = (sz > total_bytes) ? 0 : (total_bytes - sz);
            }
            double gb = (double)total_bytes / (1024.0 * 1024.0 * 1024.0);
            if (gb <= retention_max_gb_) break;
          }
        }
      }
    } catch (...) {
      // ignore
    }

    for (int i = 0; i < cleanup_interval_seconds_ && cleanup_running_; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

}  // namespace save_video
}  // namespace element
}  // namespace sophon_stream

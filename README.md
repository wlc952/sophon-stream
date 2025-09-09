# sophon-stream

## 1 快速开始**配置文件修改：**

在 `samples/yolov5` 目录下有多个config文件夹如 `config_1`，对于不同模型。

在 `samples/yolov5/config_1/yolov5_demo.json` 修改输入源，

```json
{
  "channels": [
    {
      "channel_id": 0,
      "url": "rtsp://172.24.64.225:8554/stream1",
      "source_type": "RTSP",
      "sample_interval": 1,
      "loop_num": 1,
      "fps": 25
    }
  ],
  "class_names": "../yolov5/coco.names",
  "download_image": false,
  "draw_func_name": "draw_yolov5_results",
  "engine_config_path": "../yolov5/config_1/engine_group.json"
}
```

在 `samples/yolov5/config_1/yolov5_group.json` 修改模型文件路径，

```json
{
    "configure": {
        "model_path": "/data/8models/踩刹车0825/yolov5s_tpukernel_fp16_1b.bmodel",
        "threshold_conf": 0.5,
        "threshold_nms": 0.5,
        "bgr2rgb": true,
        "mean": [
            0,
            0,
            0
        ],
        "std": [
            255,
            255,
            255
        ],
        "use_tpu_kernel": true
    },
    "shared_object": "../../build/lib/libyolov5.so",
    "name": "yolov5_group",
    "side": "sophgo",
    "thread_number": 1
}
```

在 `samples/yolov5/config_1/save_video.json` 修改保存视频和上传视频的相关参数。

```json
{
  "configure": {
    "server_url": "http://182.92.230.1:9181/warning-agreement/upload", # 改成server端真实地址
    "save_dir": "/data/alarms",
    "base_file_url": "http://127.0.0.1/static", # 改成真实设备ip
    "record_seconds": 10, # 保存视频的时长
    "pre_record_seconds": 0, # 预录时长
    "cooldown_seconds": 10, # 两次保存视频的间隔时间
    "retention_days": 1, # 保存天数
    "retention_max_gb": 1, # 最大保存空间
    "cleanup_interval_seconds": 300, # 清理间隔时间
    "deviceId": "1287", 
    "deviceIp": "127.0.0.1", # 改成真实设备ip
    "safetyId": "1",
    "safetyName": "安全员1", 
    "warning": "way",
    "video_url_field": "brakeUrl", # "safetyUrl" 或 "brakeUrl"
    "type": 7 # 对于模型检测的type 
  },
  "shared_object": "../../build/lib/libsave_video.so",
  "name": "save_video",
  "side": "sophgo",
  "thread_number": 1
}
```

**配置nginx：**

```bash
./nginx_setup.sh
```

**启动****：**

```bash
./run.sh
```

## 2 文档

请参考[sophon-stream用户文档](./docs/Sophon_Stream_User_Guide.md)

# sophon-stream 视频检测项目说明

## 1.下载项目文件（本地已有可跳过）

从网盘下载压缩包：
链接:[https://pan.baidu.com/s/11Ig7jAY9wxbdm8uh1Og4Hg?pwd=edwb](https://pan.baidu.com/s/11Ig7jAY9wxbdm8uh1Og4Hg?pwd=edwb) 提取码:edwb

## 2.解压到/data目录

unzip解压所有压缩包，请确认最终的目录结构应该为：

```sh
/data/sophon-stream  # 核心项目
/data/8models        # 测试模型
/data/test2.mp4      # 测试视频
```

## 3.开始测试

### 3.1启动nginx文件服务器

```sh
cd /data/sophon-stream
```

在目录下应该可以看到 `nginx_setup.sh`和 `nginx.conf`两个文件，执行下面命令：

```sh
chmod +x nginx_setup.sh
./nginx_setup.sh
```

### 3.2启动测试

```sh
cd /data/sophon-stream
```

在目录下应该可以看到 `run.sh`文件，执行下面的命令：

```sh
chmod +x run.sh
./run.sh
```

`run.sh`是根据需求一次运行8个模型检测任务，可使用下面的命令运行单个：

```sh
echo 'export LD_LIBRARY_PATH=/data/sophon-stream/build/lib/:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc

cd /data/sophon-stream/samples/build
./main --demo_config_path=../yolov5/config_2/yolov5_demo.json
```

### 3.3查看运行数据

可以查看盒子的文件夹 `/data/alarms/`；也可以浏览器访问nginx的网页：`http://盒子ip/static/`

## 4. 配置文件修改

在 `samples/yolov5` 目录下有多个config文件夹如 `config_1`，对于不同模型。

在 `samples/yolov5/config_1/yolov5_demo.json` 修改输入源，修改url为真实的rtsp源，对应修改source_type等。

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

重新启动：

```bash
./run.sh
```

## 5 文档

请参考[sophon-stream用户文档](./docs/Sophon_Stream_User_Guide.md)

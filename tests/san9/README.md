# 合成音频样本

`stereo.wma` 是人工生成的 3 秒、44.1 kHz 双声道 WMA v2 测试音频：左声道 440 Hz，右声道 880 Hz，码率 160 kbit/s。不包含原版游戏素材。

使用完整 FFmpeg 生成一次并提交，构建和验证精简库时无需安装 FFmpeg 命令行程序：

```bash
ffmpeg -nostdin -hide_banner -loglevel error \
  -f lavfi -i 'aevalsrc=0.2*sin(2*PI*440*t)|0.2*sin(2*PI*880*t):s=44100:d=3' \
  -c:a wmav2 -b:a 160k -map_metadata -1 tests/san9/stereo.wma
```

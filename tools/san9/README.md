# San9 音频精简构建

为三国志 IX 重制版的离线转换工具提供共享库，目标平台为 `win-x64`、`linux-x64`、`osx-arm64`。游戏客户端继续读取转换后的音频。

## 保留范围

唯一转换链为本地 ASF/WMA → WMA v2 → 44.1 kHz 双声道浮点 PCM → libvorbis 质量 5 → Ogg。只启用 `file` 协议、`asf` 解封装器、`wmav2` 解码器、`libvorbis` 编码器和 `ogg` 封装器；内部依赖由上游 configure 解析。

只构建 `libavformat`、`libavcodec`、`libavutil` 三个共享库。libogg 1.3.5 与 libvorbis 1.3.7 经过 SHA-256 校验后静态链接进去。WMA v2 解码与 libvorbis 编码都使用 FLTP，保持原有采样率和声道布局，无需重采样库。关闭其他组件、外部库自动探测、网络、程序、滤镜、视频缩放和设备库，以及 GPL/nonfree/version3 组件。

上游源码保持完整，通过专用配置裁剪产物，便于继续更新源代码。配置不改变普通 FFmpeg 的构建默认值。

## 本地构建

依赖 Bash、C 编译器、GNU Make、pkg-config、curl、tar/xz、SHA-256 工具；x86-64 构建还需要 NASM。Windows 使用 MSYS2 MINGW64 环境，Linux 使用本机 GCC，macOS ARM64 使用 Apple Clang。

```bash
bash tools/san9/build.sh linux-x64
# Windows: bash tools/san9/build.sh win-x64
# macOS:   bash tools/san9/build.sh osx-arm64
```

构建目录默认为忽略的 `.san9/build/<RID>/`，可通过 `SAN9_BUILD_ROOT` 更改；`SAN9_JOBS` 控制并行度，默认 4。产物位于其中的 `install/`。同一工作目录只用于同一平台与工具链。每次执行会重新配置并复用 make 判断仍有效的对象文件；依赖归档每次均重新核验。

构建结束会运行实际共享库的组件白名单核对，并转换仓库内人工合成的 WMA v2 测试音频。独立 libvorbisfile 解码输出，核对声道、采样率、精确采样数和相对原始 WMA 解码 PCM 的信噪比（至少 20 dB）。转换检查按累计采样数重建时间戳，排空解码器、音频 FIFO 和编码器。

```bash
bash tools/san9/verify.sh linux-x64 /path/to/input.wma /path/to/output.ogg
```

测试输入生成方式见 `tests/san9/README.md`。自检代码只用于验证，不进入发布包。FFmpeg 命令行程序不构建，也不分发；C# 消费者通过原生绑定加载这三个共享库。

## CI 与发布

`San9 audio libraries` 工作流在三平台原生构建，PR、master 推送和手动执行都会验证；推送 `san9-audio-v*` 标签时，三平台全部通过后发布该标签的构建。打包仅包含三个共享库及符号链接／Windows 导入库、开发头文件、构建记录和许可证。动态库按 `runtimes/<RID>/native/` 布置。构建记录包含源提交、依赖版本、哈希与 configure 参数。

依赖源代码下载入口：

- https://downloads.xiph.org/releases/ogg/libogg-1.3.5.tar.xz
- https://downloads.xiph.org/releases/vorbis/libvorbis-1.3.7.tar.xz

FFmpeg 对应源码为构建记录指向的本仓库提交，包括本目录内的构建脚本；许可证随产物放入 `share/san9/licenses/`。共享库调用方负责输入范围检查、输出发布和错误处理，语义命名与播放绑定属于重制版转换工具。

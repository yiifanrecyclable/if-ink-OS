# if-ink OS v0.1.6 构建与重新链接

项目身份保留 `Read_Pico_IF`，用于兼容已安装的 IF 本地升级。源码包括应用、修改后的 epdiy、固定依赖和实际构建配置。应用 SHA256：`3309df71c9c130c261fa5933084d7d3deb91f6af7f7a715b8abd570df478f7df`。

安装 Espressif ESP-IDF 与工具链，SDK 固定提交 [`fff9895c82d744c7237be8847347bdd1b07c6643`](https://github.com/espressif/esp-idf/tree/fff9895c82d744c7237be8847347bdd1b07c6643)（v6.1 开发树）。按 SDK 官方说明执行 install/export 脚本，然后在本仓库执行：

```sh
idf.py -B build -D SDKCONFIG=sdkconfig.release build
```

实际发布配置为 `sdkconfig.release`；依赖版本在 `dependencies.lock`，对应源码在 `components/` 与 `managed_components/`。SDK/工具链首次安装需联网。构建包含实际内建字库，不需要先装字体生成依赖；如修改内建字库，按 `tools/gen_builtin_font.py` 及 `assets/fonts/README.md` 说明准备字体。完整 TF 字体和拼音词库另在资源包。

替换 `components/epdiy` 中接口兼容的实现或修改应用后，执行相同命令重新编译链接；按新构建的 `build/flash_args` 安装自己的四段产物，不能用原发布应用替代自己的重新链接结果。未启用安全启动或 Flash 加密来限制更换固件。编译时间会影响镜像摘要，不要求逐字节重现。源码中的 `sdkconfig.defaults` 是板级产品默认，CI 配置不能替代它。

主体 Apache-2.0；epdiy LGPL-3.0-or-later。随版本提供完整可修改应用源码、库源码、固定配置与构建说明，允许修改库及调试相关改动；原许可、上游修改说明和其他组件许可见 `LICENSE`、`THIRD_PARTY_NOTICES.md`、组件目录与下载许可包。

来源：[Pico Reader / kiikoread](https://github.com/wegooo-cell/read-pico-reader)；[原作者主页](https://wegooo-cell.github.io/read-pico-reader/)。if-ink OS 是独立衍生版本。

# Mixxx OHOS HAP 壳工程（TASK-003）

Qt for HarmonyOS 官方模板结构（源自 `qtbase/src/harmonyos/templates`），
用于把 Mixxx 以 HAP 形式打包并在 HarmonyOS NEXT 真机启动。

## 结构

```
packaging/ohos/
  AppScope/app.json5              bundleName: com.pomelo.mixxx
  build-profile.json5             products / signingConfigs / SDK 6.1.1(24)
  entry/
    build-profile.json5           无 externalNativeOptions（native 库由 build-hap.sh 预编）
    src/main/cpp/                 Qt 启动库源码（qtmixxxboot.cpp + Main.qml）
    src/main/ets/                 Qt QPA 的 ArkTS 宿主（QAbility/QAbilityStage，模板原样）
    src/main/module.json5         入口 Ability 声明
    libs/arm64/                   build-hap.sh 填充：Qt 库/插件/QML + qtmixxxboot.so (+libmixxx.so)
```

## 启动链

```
HAP (EntryAbility)
  -> QAbilityStage.ets  qpa.setupQtApplication({appName: "libqtmixxxboot.so"})
  -> libqohos.so (Qt OHOS QPA)  dlopen(bundleCodeDir/libs/arm64/libqtmixxxboot.so)
  -> qtmixxxboot::main()  QGuiApplication + QQmlApplicationEngine
  -> 内嵌 QML（qrc:/qt/qml/mixxx/ohos/boot/Main.qml）
```

TASK-004 起把 `APP_LIBRARY_NAME` 换成 `libmixxx.so`（Mixxx 的 main()）。

## 构建 / 签名 / 安装

```bash
# 1) 构建 + 打包（Docker，产出 entry-default-unsigned.hap）
docker run --rm -i \
  --mount "type=bind,src=D:/Git/mixxx,dst=/data/src/mixxx" \
  --mount "type=bind,src=D:/Git/mixxx/sign,dst=/data/sign" \
  --mount "type=bind,src=F:/command-line-tools,dst=/apps/harmony" \
  --mount "type=volume,src=mixxx-ohos-qt-out,dst=/data/out" \
  --mount "type=volume,src=mixxx-ohos-vcpkg,dst=/data/vcpkg" \
  --mount "type=volume,src=mixxx-ohos-mixxx-build,dst=/data/mixxx-build" \
  winehua-dev bash /data/src/mixxx/packaging/ohos/build-hap.sh

# 2) 命令行签名（hap-sign-tool；证书必须与 p12 私钥、profile 证书三者配对）
#    见 scripts/sign-hap.sh，密码/别名从环境变量读取
```

## 签名材料要求（重要）

HarmonyOS NEXT 真机安装要求 **三者匹配**：

| 材料 | 说明 |
|---|---|
| 私钥 keystore | `.p12`，别名 + 密码；公钥必须与下面证书一致 |
| 应用证书 | `.cer`，Huawei CBG 颁发（AGC「调试证书」下载） |
| Profile | `.p7b`，bundle-name 必须是 `com.pomelo.mixxx`，内嵌的 development-certificate 必须是上面同一张证书，且 device-ids 含目标设备 UDID |

校验方法（三者公钥/证书指纹必须一致）：

```bash
# profile 内嵌证书
java -jar hap-sign-tool.jar verify-profile -inFile x.p7b -outFile vr.json
# p12 内证书
keytool -exportcert -alias <alias> -keystore x.p12 -rfc -file ad.pem
# 应用证书
diff ad.pem x.cer        # 必须完全一致
```

推荐用 DevEco Studio「自动签名」生成整套材料：
打开本目录为工程 → File > Project Structure > Signing Configs →
勾选 "Automatically generate signature" → 登录华为账号 → OK。

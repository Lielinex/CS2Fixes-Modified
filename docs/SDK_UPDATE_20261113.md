# 2026-11-13 CS2更新兼容修复

在Lielinex仓库的本地`dev`分支工作，基线为`687f51acb2b717218f6e1e42710f14cfd58a1960`。origin为`https://github.com/Lielinex/CS2Fixes-Modified.git`；upstream仅用于读取，推送地址为DISABLED。本轮不创建PR、不推送、不提交到远程。

## 改动依据

- 原作者[9834531 Update SDK](https://github.com/Source2ZE/CS2Fixes/commit/9834531b3f9a599e477273a921e04b4f96e5b86d)：将`sdk`从`30c1a859fe846846081ce0e6125c23e497a9a186`更新至`2283c1f919cb7b75329eb059ef6b064b36c2fdaa`，并调整调用方。新SDK以`NetMessageInfo_t`直接描述消息，原先的`INetworkMessageInternal`封装与虚调用不再适用；注册消息、事件Hook、聊天、感染震屏、投票及HUD调用均同步调整。ClientPutInServer Hook参数改为SDK的`ClientPutInServerType_t`。
- 原作者[f5551d7 Update gamedata](https://github.com/Source2ZE/CS2Fixes/commit/f5551d7b65eef7708e730435262f6b4927d98011)：移植`CS_Script_SetModel`的Windows/Linux签名更新。本机旧签名没有匹配，更新后唯一匹配。
- 修正ClientPutInServer调试输出：原格式串有5个占位符却只传4个参数；现在明确提取slot、枚举和64位ID，避免可变参数越界读取。
- 本地二进制版本附加`sdkfix.1`，便于区分基线。现有ZR配置和其余功能保留原分支行为。

## 本机验证环境

Windows x64；游戏PatchVersion `1.41.9.7`，ServerVersion `2000943`，SourceRevision `11228971`。扫描实际安装的server.dll、engine2.dll和worldrenderer.dll。签名匹配只验证模式位置，不能代替函数布局、ABI和运行测试。

旧版CS2Fixes共49个Windows签名，48个唯一匹配，`CS_Script_SetModel`缺失。本地修复版49个全部唯一匹配。原始报告保存在`C:/CS2/.build/cs2fixes-update-20261113/signature-scan.json`。

隔离基线测试使用127.0.0.1:27025，仅通过Metamod加载旧CS2Fixes，并暂用修正后的gamedata。严格等待地图进入Running状态后，执行`bot_add_ct`导致进程以`0xC0000005`退出；证据在`old-sdk-strict-20261113-234748/`。Windows Application事件1000确认故障模块为`cs2fixes.dll`，偏移`0x78968`，两次有效复现一致。测试结束已校验gamedata恢复，线上DLL未替换。首次路径配置不正确的测试不作为插件验证结果。

## 构建

已按根AGENTS要求尝试`docker compose up`，当前Windows主机没有Docker。Windows原生构建沿用上游README/CI的AMBuild流程。便携Python 3.13.9、MSVC 14.52.37025、Windows SDK 10.0.28000.0、AMBuild及Metamod依赖保存在`C:/CS2/.build/cs2fixes-toolchain/`，不修改全局PATH。工具仅在当前构建进程设置环境。Metamod源码为`ea513d8`，AMBuild为`5b0aa72`。

```powershell
./scripts/Build-Local.ps1 -ToolchainRoot C:/CS2/.build/cs2fixes-toolchain -Jobs 4
```

SDK保持上游指定提交，内部没有额外修改。编译输出在`build/package/cs2/`。在其他电脑重建需准备同样的Windows x64工具、AMBuild及Metamod依赖，再设置脚本参数；不应使用`git submodule update --remote`浮动到未知SDK版本。

## 结果与范围

Windows x64 Release编译成功，无编译/链接错误。有现有代码的类型转换、this初始化、Steam头文件编码、/Zi与/Z7重复以及SDK静态库缺少PDB的警告，未将警告当作已修复。构建日志为`C:/CS2/.build/cs2fixes-update-20261113/windows-build.log`，版本信息补齐后的重链记录为`windows-build-final.log`。最终版本`v2.1-0-687f51a-sdkfix.1`。

修复版隔离测试已通过：进入Running状态、机器人连接及ClientPutInServer Hook、`mp_restartgame 1`、换图到de_dust2，以及换图后的插件/服务器状态查询；五次管理指令均返回成功，进程未崩溃。最终交付DLL于2026-11-14再次按相同流程验证，证据目录为`C:/CS2/.build/cs2fixes-update-20261113/sdkfix1-final-20261114-000021/`，SHA256为`AA7486367466FDBCE65D3139B40422E71DA0E0DC7558978B6ECE70DEBE8062CE`。

所有测试仅运行CS2Fixes，不包含CounterStrikeSharp等其余插件，也没有真人连接；不代表完整插件组合、真人网络消息或所有ZE地图已验收。当前完整服务器还有另一个CounterStrikeSharp地址签名未匹配的独立线索，见扫描报告，未在本轮修改该插件。

隔离验证结束时正式服务器DLL保持原哈希`5256E4FB802C8A86C73BC513AADC88F324EC87DB738621796EAE176B3BE31552`，临时gamedata逐次恢复。修复保留为本地dev工作区改动，没有本地提交或推送。若安装交付包，须停止服务器并备份原DLL、PDB和gamedata，再替换包内对应文件；配置文件保持原样。

## 正式服部署（2026-11-14）

随后用户明确要求替换正式服并打开控制台供进服测试。已备份原DLL、PDB和gamedata至`C:/CS2/backups/cs2fixes-sdkfix1-deploy-20261114-000622/`，替换修复包三个文件，DLL哈希与隔离测试一致。完整服仍有旧运行组件的兼容问题，随后备份并更新官方[CounterStrikeSharp 1.0.380](https://github.com/roflmuffin/CounterStrikeSharp/releases/tag/v1.0.380)和[MultiAddonManager 1.7.4](https://github.com/Source2ZE/MultiAddonManager/releases/tag/v1.7.4)，两者均发布了对应本次游戏更新的修复。管理员、核心配置和工坊列表按哈希校验保留。

服务器已在可见控制台运行，RCON确认修复版、上述两个运行组件、11个托管插件正常加载，HUD ready且无故障，Steam secure，地图为ze_purification_a1，训练机器人已进入。对frp-net.com:63242的A2S查询成功。真人连接与完整训练流程由用户接着测试。部署及框架备份记录分别在`C:/CS2/server-platform/cs2fixes-sdkfix-deployment.json`和`game-update-frameworks.json`。交付ZIP保留了部署前的隔离验证快照，后续部署状态以本节和运行记录为准。

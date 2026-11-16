# ZE训练ZR

训练开关默认关闭，保留普通ZR。训练服需同时启用ZR并安装配套ZombieEscapePractice `1.1.3-caszep.7`；旧ZEP仍会争夺机器人控制，不能混用。职业、武器、hitgroup配置继续使用原ZR格式。

```cfg
zr_enable 1
zr_training_enable 1
```

将配置写入服务器每张地图加载的配置；ZR职业及资源需在地图预缓存前启用。切换`zr_training_enable`在下一回合生效，管理员可用`mp_restartgame 1`重开。切图也会应用新配置。不要把输出状态变量当成设置项。

## 行为

- 一个禁用AI和攻击的训练机器人，初始感染在冻结结束后3秒发生，采用普通僵尸默认职业血量。真人在所有感染入口保持人类CT，刀伤仍可致死。
- 真人死亡不自动复活，不改阵营；回合开始前10秒允许首次入场，达到10秒后等待下一回合。重连、换队或其他插件直接Respawn不能绕过限制。
- 地图`zr_toggle_respawn`只控制机器人的自动重生，不复活真人。机器人正常被地图nuke杀死后，以实际CT胜利结算，不依赖实体名称或额外延迟。
- 空服首人触发新回合，此新回合正常计通关；清理旧回合不计。机器人异常掉线/被踢或创建失败使当前回合无效；补齐后开启新回合恢复，即使地图此前已关闭重生也可恢复。
- 预留一个机器人名额，保持`bot_quota_mode fill`、quota=真人数+1、`bot_auto_vacate 0`；不踢真人腾位置。正常模式恢复进入训练前的机器人相关配置。
- 管理员既有进服观察偏好和主动观察保留。申请加入T按CT处理，出生仍受限制。

## 原生与ZEP接口：protocol 1

| 输出 | 含义 |
| --- | --- |
| `zr_training_protocol` | 固定1；观察器要求匹配 |
| `zr_training_session` | 进程启动标识+回合代际；换图/新回合变化，ZEP热重载沿用，用作后端去重键 |
| `zr_training_round` | 本进程回合代际，地图初始化和round_prestart时更新 |
| `zr_training_state` | 0普通ZR、1准备、2有效训练已感染、3异常无效 |

状态在实际round_end回调期间保留，直到下一轮初始化。异常状态在当前轮不可重新激活。ZEP只在同一session、state=2、实际CT胜利且有已认证存活真人CT时上报一次，不再读取机器人的死亡缓存。普通ZR不计训练通关。

`zr_training_status`在服务器控制台显示请求/生效状态、回合、真人数和机器人槽位；`css_zep_zrstatus`显示观察器读取的状态。两者均不接受普通客户端调用。

## 明确复活

服务器命令`zr_training_respawn #userid [session]`用于明确的CT复活。客户端不能直接执行；带session时必须与当前原生回合匹配，避免排队旧命令作用于新回合。授权仅作用于这一次同步出生，不是临时开放所有重生。

ZEP提供`css_zep_respawn #userid`和`css_zep_respawn @all`，需`@css/root`或`@css/respawn`权限，服务器控制台可用。此命令用于真正复活死者；原`c_revive`仍是治愈存活僵尸，二者不能混淆。第三方直接Respawn不自动视为管理员授权，应通过明确入口调用。

获授权的训练节点复位会先复活等待/死亡的CT参与者，再执行节点命令和传送；权限及回合在延迟执行前重新检查。机器人不由ZEP复活、治愈或感染。PMC保存的CT模型在真人出生及新回合后恢复。

## 构建与维护

根AGENTS要求的`docker compose up`构建Linux包；本机未安装Docker，使用已有便携工具链执行：

```powershell
./scripts/Build-Local.ps1 -Jobs 4
./scripts/Build-Local.ps1 -TestTrainingPolicy
```

SDK锁定于gitlink，不使用`submodule update --remote`。SDK修复独立提交`0a55951`；训练实现另行提交。只推送`origin/dev`，不推送upstream或自动合并main。ZEP及平台接口的改动/构建分别保留在相邻开发仓库。

部署时备份并成组替换CS2Fixes DLL/PDB和配套ZEP DLL/PDB；启用配置后重载地图。保持既有ZR职业、PMC、平台身份及积分配置。回滚需同时恢复旧原生与ZEP版本及旧开关，不能让旧ZEP和新原生同时控制机器人。

验证结果及真实游戏验证的边界见[验证记录](TRAINING_ZR_VALIDATION.md)。

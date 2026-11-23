# 原作配装回归

此回归使用正常档案 RPC、持有房间凭据的出生接口、两个隔离测试账号和临时存档。它不会打开游戏，也不读取实际玩家的存档。

```powershell
node all-loadouts.test.cjs '<BoundaryMetaServer-main>' '<BoundaryToolbox>' '<output.json>'
node migrate-old-paint.test.cjs '<BoundaryMetaServer-main>' '<19.20.32 frozen source>' '<BoundaryToolbox>' '<migration.json>'
```

[data/catalog.json](data/catalog.json) 列出角色可配枪、逐槽配件以及原作表来源摘要。它仅供测试，不是运行时的解锁表。测试会校验拒绝越权选择、逐把主副换位、去掉配件、重登、服务重启及同伴隔离。维护版的武器机匣和航天服允许表分别位于 [receiver-main.json](../receiver-main.json) 和 [role-paint-scopes.json](../role-paint-scopes.json)。

最终结果 区分源数据的 46 把枪、当前普通房可选的 19 把枪，以及游戏内尚未通过的部分。

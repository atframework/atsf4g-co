@echo off
cd /d %~dp0

..\atdtool\atdtool.exe template ..\..\cloud-native\charts -o ..\..\  --values ..\..\cloud-native\values\default,..\..\cloud-native\values\dev --set global.world_id=1
@echo off
cd /d %~dp0

..\atdtool\atdtool.exe template ..\..\cloud-native\charts -o ..\..\  --values ..\..\cloud-native\values\default --set global.world_id=1
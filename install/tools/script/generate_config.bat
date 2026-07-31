@echo off
cd /d %~dp0

..\atdtool\atdtool.exe template ..\..\cloud-native\charts -o ..\..\  --values ..\..\cloud-native\values\default,..\..\cloud-native\values\dev,..\..\cloud-native\values\personal --set global.world_id=1

..\atdtool\atdtool.exe template ..\..\cloud-native\charts\robot -o ..\..\robot --values ..\..\cloud-native\values\default,..\..\cloud-native\values\dev,..\..\cloud-native\values\personal --mode nondeploy
..\atdtool\atdtool.exe template ..\..\cloud-native\charts\otelcol -o ..\..\otelcol --values ..\..\cloud-native\values\default,..\..\cloud-native\values\dev,..\..\cloud-native\values\personal --mode nondeploy
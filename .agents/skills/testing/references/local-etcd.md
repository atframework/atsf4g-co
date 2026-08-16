# Local etcd for unit tests

Read this only for `atapp_etcd_cluster`, `atapp_etcd_module`, or another test that explicitly requires
`ATAPP_UNIT_TEST_ETCD_HOST`.

Use the scripts in `atframework/libatapp/ci/etcd/`:

```bash
bash atframework/libatapp/ci/etcd/setup-etcd.sh start
export ATAPP_UNIT_TEST_ETCD_HOST="http://127.0.0.1:12379"
# Run the selected tests.
bash atframework/libatapp/ci/etcd/setup-etcd.sh stop
```

```powershell
.\atframework\libatapp\ci\etcd\setup-etcd.ps1 -Command start
$env:ATAPP_UNIT_TEST_ETCD_HOST = "http://127.0.0.1:12379"
# Run the selected tests.
.\atframework\libatapp\ci\etcd\setup-etcd.ps1 -Command stop
```

The helpers support `download`, `start`, `stop`, `cleanup`, and `status`; the default client port is `12379`. If the
environment variable is absent, these cases are skipped rather than failed. Stop the instance after the test run; use
the destructive `cleanup` action only when its deletion is explicitly intended.

# Server stack

## Khởi động

```bash
cd server
docker compose up -d
```

## Truy cập

| Service   | URL                       | Tài khoản           |
|-----------|---------------------------|---------------------|
| InfluxDB  | http://localhost:8086     | admin / usth-sensor-2026 |
| Grafana   | http://localhost:3000     | admin / admin       |
| Node-RED  | http://localhost:1880     | (không auth)        |
| MQTT      | tcp://localhost:1883      | anonymous           |
| MQTT-WS   | ws://localhost:9001       | anonymous           |

## Đổi mật khẩu / token

Trước khi demo "thật", đổi:
- `DOCKER_INFLUXDB_INIT_ADMIN_TOKEN` trong `docker-compose.yml`
- `token = "..."` trong `telegraf/telegraf.conf`
- `secureJsonData.token` trong `grafana/provisioning/datasources/influxdb.yml`

Cả 3 phải khớp nhau.

## Kiểm tra pipeline

```bash
# Subscribe để xem ESP32 có publish không
mosquitto_sub -h localhost -t 'usth/pdr/#' -v

# Publish thử
mosquitto_pub -h localhost -t 'usth/pdr/dev01/activity' \
  -m '{"ts":1735300000000,"class":"walk","confidence":0.93}'
```

Sau đó vào InfluxDB UI → Data Explorer → query bucket `pdr` để xác nhận data đã ghi.

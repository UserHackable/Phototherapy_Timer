# Secrets (local only)

| File | In git? | Contents |
|------|---------|----------|
| `wifi.yaml.example` | yes | Template |
| `wifi.yaml` | **no** | Real SSIDs + passwords |
| `generated/` | **no** | Headers produced for firmware |

```bash
cp secrets/wifi.yaml.example secrets/wifi.yaml
# edit passwords
./scripts/gen-wifi-credentials.sh
./scripts/fw idf upload wifi_connect
```

See [docs/wifi-config.md](../docs/wifi-config.md).

# Deployment Checklist ✅

Use this checklist to deploy the auto-configuration system.

---

## Phase 1: Deploy Cloudflare Worker

- [ ] Install Wrangler CLI: `npm install -g wrangler`
- [ ] Login to Cloudflare: `wrangler login`
- [ ] Edit `cloudflare-worker/worker.js`:
  - [ ] Change `EXPECTED_AUTH` to your secret (line 49)
  - [ ] Add your Gemini API key (line 56)
- [ ] Deploy worker: `cd cloudflare-worker && wrangler deploy`
- [ ] Copy worker URL (e.g., `https://bubu-config.USERNAME.workers.dev`)
- [ ] Test with curl:
  ```bash
  curl -H "X-Bubu-Auth: YOUR_SECRET" \
    https://bubu-config.USERNAME.workers.dev/key
  ```
  Should return JSON with your API key

---

## Phase 2: Update ESP32 Code

- [ ] Edit `src/config_fetcher.cpp`:
  - [ ] Line 13: Paste your worker URL
  - [ ] Line 17: Paste your auth token (must match worker)
- [ ] Save file

---

## Phase 3: Build & Test

- [ ] Build firmware: `pio run`
- [ ] Check build succeeded (no errors)
- [ ] Flash to test device: `pio run -t upload`
- [ ] Open serial monitor (115200 baud)
- [ ] Connect device to WiFi (via menu or auto-connect)
- [ ] Watch for auto-configuration:
  ```
  [Loop] WiFi connected, attempting auto-configuration...
  [Config] Configuration saved to NVS successfully!
  [Loop] ✅ Auto-configuration successful!
  ```
- [ ] Verify chat works (say "hello")

---

## Phase 4: Test Fallback

- [ ] Clear API key: Serial command `chat key ""`
- [ ] Reboot device
- [ ] Watch for auto-config again
- [ ] Should auto-configure successfully

---

## Phase 5: Upload to GitHub

- [ ] Verify no hardcoded API key in code:
  ```bash
  grep -r "AIzaSy" src/ include/
  # Should find nothing
  ```
- [ ] Build firmware: `pio run`
- [ ] Commit changes:
  ```bash
  git add .
  git commit -m "Add auto-configuration system"
  git push
  ```
- [ ] Upload firmware binary to GitHub Releases
- [ ] Verify **no GitHub secret warnings** ✅

---

## Phase 6: Document for Users

- [ ] Update README with auto-config info
- [ ] Add setup instructions
- [ ] Mention WiFi required for first boot

---

## Optional: Advanced Testing

- [ ] Test with wrong auth token (should fail with 401)
- [ ] Test with worker down (should fall back to serial)
- [ ] Test key rotation:
  - [ ] Update worker with new key
  - [ ] Deploy: `wrangler deploy`
  - [ ] Clear device NVS
  - [ ] Reboot device
  - [ ] Should get new key

---

## Troubleshooting Checklist

### Auto-config not working?

- [ ] WiFi connected? Check `wifiGetState()`
- [ ] Worker URL correct in `config_fetcher.cpp`?
- [ ] Auth token matches between ESP32 and worker?
- [ ] Worker deployed successfully?
- [ ] Worker returns valid JSON? Test with curl
- [ ] Check serial logs for error messages

### Worker issues?

- [ ] Logged in to Cloudflare? `wrangler whoami`
- [ ] Worker deployed? `wrangler deployments list`
- [ ] Worker accessible? Test URL in browser
- [ ] Logs available? `wrangler tail`

### GitHub still warning?

- [ ] Old firmware with hardcoded key?
- [ ] Rebuild with latest code
- [ ] Verify no hardcoded keys: `grep -r "AIzaSy"`

---

## Success Criteria

✅ Worker deployed and accessible
✅ ESP32 code updated with worker URL
✅ Device auto-configures on WiFi connect
✅ Chat system works after auto-config
✅ Firmware uploaded to GitHub without warnings
✅ No API key in source code or binary

---

## Rollback Plan

If something goes wrong:

### Option 1: Revert to Serial Config
- [ ] Comment out auto-fetch code in `main.cpp`
- [ ] Configure devices manually via serial

### Option 2: Use Fallback
- [ ] Auto-config failing is OK
- [ ] Serial commands still work
- [ ] Configure manually: `chat key YOUR_KEY`

---

## Post-Deployment

### Monitor worker usage:
```bash
wrangler tail  # Real-time logs
```

### Check Cloudflare dashboard:
- Request count
- Success/error rates
- Response times

### Rotate keys periodically:
1. Update `worker.js` with new API key
2. Deploy: `wrangler deploy`
3. Clear NVS on devices to force re-fetch

---

## Summary

**Time Required:** ~15 minutes
**Cost:** $0 (Cloudflare free tier)
**Difficulty:** Easy (copy-paste configuration)

**Result:**
✅ Devices auto-configure themselves
✅ No GitHub warnings
✅ Easy key management
✅ Professional deployment system

---

## Next Steps After Deployment

1. ✅ Test with 2-3 devices
2. ✅ Monitor for issues
3. ✅ Update documentation
4. ✅ Announce to users
5. 🎉 Enjoy auto-configuration!

---

**Ready to deploy? Start with Phase 1!** 🚀

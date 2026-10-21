# IF verified installer runtime

`installer.js` and `install-button.js` are the maintained IF runtime sources. The version/size/hash pins in the loader are substituted from an already prepared local release. No firmware is built, erased, flashed or published by this staging command:

```sh
python3 tools/stage_if_webflash_runtime.py /absolute/path/to/site 0.1.3
node tools/test_if_flash_snapshot.cjs /absolute/path/to/site
node tools/test_if_webflash.cjs /absolute/path/to/site 0.1.3
```

The site must already contain the four verified files and `webflash/0.1.3/manifest.json`, plus the existing `webflash/vendor/esp-web-tools-if-v1/` vendor files. Staging replaces only the loader and its entry adapter. It does not change the website layout, release files, manifests or the existing no-erase policy. Use this step when preparing the next publication; it does not deploy the local changes automatically.

After the user picks a port, all four actual downloads are checked against the selected manifest's size/SHA256; the application project/version is also checked. Only then does the adapter open the port. ESP Web Tools receives Blob URLs for the verified manifest and image bytes; close/failure releases those URLs. Image-internal SHA handling in esptool is separate from this manifest-level bundle verification.

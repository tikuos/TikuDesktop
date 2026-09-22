# A complete 2x3 BMP, including NULs; confined to the harness's fixture home.
printf '%s' \
    'Qk1OAAAAAAAAADYAAAAoAAAAAgAAAAMAAAABABgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAD/AP8AAAD/AAD//wAAAP///wAAAAAA' \
    | base64 -d > "$deskhome/snapshot.bmp"

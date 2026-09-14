# A binary fixture with embedded NULs; no device or user document is touched.
mkdir -p "$deskhome/hexfiles"
for i in 1 2 3 4 5 6 7 8; do
    printf '\000\001\002\003\004\005\006\007\010\011\012\013\014\015\016\017TikuOS binary sensor snapshot....'
done > "$deskhome/hexfiles/snapshot.bin"
cp "$top/build/apps/hex/TikuHex.so" "$appsdir/"

# A ZIP with a folder in it and a tar.gz, confined to the harness's fixture
# home, in two cases of name to exercise the association.  TikuArchive
# joins this lane's applications only, so no other lane's menus change.
cp -R "$top/build/runtime/apps/archive" "$appsdir/"
mkdir -p "$deskhome/archives"
printf '%s' \
    'UEsDBBQAAAAAAFxkoVgAAAAAAAAAAAAAAAAEAAAAc2RrL1BLAwQUAAAACABcZKFYOnpKnRgA' \
    'AABUAQAADgAAAHNkay9yZWFkbWUudHh0C8lIVUjKTyxKUS9WCHbx1uMKGRWgVAAAUEsDBBQA' \
    'AAAIAFxkoVjpBJ/rEgAAABAAAAATAAAAc2RrL2luY2x1ZGUvYm9hcmQuaFNOSU3LzEtVcPJ3' \
    'DHJRMOQCAFBLAwQUAAAACABcZKFYFzMnEQ0AAAALAAAABwAAAExJQ0VOU0VzLEhMzkjVNdIz' \
    '4AIAUEsBAhQDFAAAAAAAXGShWAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAO1BAAAAAHNkay9Q' \
    'SwECFAMUAAAACABcZKFYOnpKnRgAAABUAQAADgAAAAAAAAAAAAAApIEiAAAAc2RrL3JlYWRt' \
    'ZS50eHRQSwECFAMUAAAACABcZKFY6QSf6xIAAAAQAAAAEwAAAAAAAAAAAAAApIFmAAAAc2Rr' \
    'L2luY2x1ZGUvYm9hcmQuaFBLAQIUAxQAAAAIAFxkoVgXMycRDQAAAAsAAAAHAAAAAAAAAAAA' \
    'AACkgakAAABMSUNFTlNFUEsFBgAAAAAEAAQA5AAAANsAAAAAAA==' \
    | base64 -d > "$deskhome/archives/sdk.zip"
printf '%s' \
    'H4sICPA2MmYC/3Rvb2xzLnRhcgDt0jsOwjAQRdGpswojemzjT9YTIpCRIixhs3+MO+gDirin' \
    'eaNppnhTc16KllWZZgyhZ/OZfbY+Wu+dc/G1H20wooJ8waPU6d5Oyn+qvf/LMpV0KOlH/R/D' \
    'e//WRGNFGfpf3X6nT9ebLmk4zymr/giDAAAAAAAAAAAAAAAAANiCJ3R1RpoAKAAA' \
    | base64 -d > "$deskhome/archives/TOOLS.TAR.GZ"

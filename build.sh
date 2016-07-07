mkdir -p ./build/
gbp buildpackage \
 --git-debian-branch=rtcan_select \
 --git-export-dir=./build/ \
 -uc -us

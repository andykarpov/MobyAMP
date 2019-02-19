#!/bin/bash

boards=(interface interface2 tda2030a tda1557q msgeq7 pt2322)
ver="v_1_0"
exts=(pdf svg png ps)

for board in ${boards[*]}
do
    ./create_gerbers.sh ${board}/amp_${board}_${ver}.brd build/gerbers/${board}/${board}_${ver}
    ./fix_drd.sh build/gerbers/${board}/${board}_${ver}.drd
    for ext in ${exts[*]}
    do
	./create_images.sh build/gerbers/${board}/${board}_${ver}.sol build/${ext}/${board}_${ver} ${ext}
    done
done

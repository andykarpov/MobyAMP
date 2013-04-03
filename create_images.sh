#!/bin/bash
# andy.karpov@gmail.com - Use freely, for whatever, at your own risk.

if [ $# -lt 2 ] ; then
        echo "usage $0 <path-to-sol> [output-dir-and-file-prefix]"
        echo " e.g:  $0 my_boards/boardx.sol pdfs/board/someboard pdf"
        echo "  will create pdf files with pcb layers in a directory called pdfs/board"
        exit 1
fi

PCB="${1//.sol}"
outputfile=${2:-"$(dirname $PCB)/$(basename $PCB .sol)"}
outputdir=$(dirname ${outputfile})
ext="$3"

if [ ! -d ${outputdir} ]; then
    mkdir -p ${outputdir}
fi

set -e

OUT_BOTTOM=${outputfile}_pcb_bottom
OUT_TOP=${outputfile}_pcb_top
OUT_CMP=${outputfile}_pcb_cmp
BG_COLOR="#FFFFFF"
DR_COLOR="#0000FFFF"
DR_COLOR2="#FFFFFFFF"
FG_COLOR="#000000FF"

# components bottom (drills + solder layer + border)
gerbv -x${ext} --dpi=600 --border=0 -o$OUT_BOTTOM.${ext} -b$BG_COLOR -f${DR_COLOR2} -f${FG_COLOR} -f${FG_COLOR} $PCB.drd $PCB.sol $PCB.bor
# components bottom (drills + solder layer + border)
gerbv -x${ext} --dpi=600 --border=0 -o$OUT_TOP.${ext} -b$BG_COLOR -f${DR_COLOR2} -f${FG_COLOR} -f${FG_COLOR} $PCB.drd $PCB.cmp $PCB.bor
# components, top (placements + drills + border)
gerbv -x${ext} --dpi=600 --border=0 -o$OUT_CMP.${ext} -b$BG_COLOR -f${FG_COLOR} -f${DR_COLOR} -f${FG_COLOR} $PCB.plc $PCB.drd $PCB.bor

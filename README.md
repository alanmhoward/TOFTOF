# TOFTOF
Convert TOFTOF mdat files into ROOT format

mdat2root.C : ROOT script for converting mdat output from QMesydaq to ROOT format example usage (batch conversion) - for i in ../data/*.mdat; do root -q -b 'mdat2root.C("'$i'")'; done

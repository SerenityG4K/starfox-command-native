"""Recompilation statique ARMv5TE → C.

- `insn.py`     : représentation unifiée d'une instruction (ARM ou Thumb)
- `armdec.py`   : décodeur ARM (mots de 32 bits)
- `thumbdec.py` : décodeur Thumb (demi-mots de 16 bits, `bl` sur deux)
- `emit.py`     : traduction d'une instruction en C
- `recomp.py`   : pilote : cartographie + ROM → fichiers C
"""

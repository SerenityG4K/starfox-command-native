cmake_minimum_required(VERSION 3.20)
project(sfc_recomp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Code généré depuis la ROM par tools/recomp/recomp.py (non versionné)
set(GENERATED_DIR "${CMAKE_SOURCE_DIR}/../generated" CACHE PATH "répertoire du C généré")
file(GLOB GENERATED_SOURCES "${GENERATED_DIR}/*.c")
if(NOT GENERATED_SOURCES)
  message(FATAL_ERROR "aucun fichier dans ${GENERATED_DIR} : lancez tools/recomp/recomp.py")
endif()

add_library(recomp STATIC ${GENERATED_SOURCES})
target_include_directories(recomp PUBLIC "${CMAKE_SOURCE_DIR}" "${GENERATED_DIR}")
# le code généré est énorme : pas d'optimisation lourde, pas d'avertissements, pas d'informations
# de débogage (elles décuplent la taille des objets et de l'exécutable ; les fautes sont
# diagnostiquées par le runtime avec l'état ARM), et pas de fichiers temporaires (-pipe)
# SFC_OPT=-O2 pousse l'optimisation du code généré : la simulation est le gros du temps d'une
# image, mais huit millions de lignes se compilent d'autant plus lentement. -O1 par défaut.
set(SFC_OPT "-O1" CACHE STRING "optimisation du code généré (-O1 ou -O2)")
target_compile_options(recomp PRIVATE ${SFC_OPT} -g0 -w -fno-strict-aliasing -pipe)
# SFC_PC_TRACE=ON : le code généré note le PC invité avant chaque instruction (r15), pour
# attribuer une écriture observée (SFC_WATCH) à son instruction ; plus lent, à part (build/runtime_pc)
option(SFC_PC_TRACE "PC invité à jour avant chaque instruction (diagnostic)" OFF)
if(SFC_PC_TRACE)
  target_compile_definitions(recomp PRIVATE RT_TRACE)
endif()

# SFC_COMPTE_TRADUIT=ON : chaque fonction traduite compte ses exécutions (SFC_TRADUIT=fichier).
# Avec SFC_NATIF=entier, ce qui reste est ce que le jeu doit encore à la traduction.
option(SFC_COMPTE_TRADUIT "compter les exécutions du code traduit (mesure)" OFF)
if(SFC_COMPTE_TRADUIT)
  target_compile_definitions(recomp PRIVATE SFC_COMPTE_TRADUIT)
endif()

# SFC_ARM7=ON : la traduction du coprocesseur est liée elle aussi, et SFC_ARM7=1 à l'exécution
# la met aux commandes à la place de la simulation de son rôle. Ses symboles portent le suffixe 7
# (g_recomp7_table, g_fn7_hooks) ; son en-tête functions.h lui est propre, d'où l'ordre des
# répertoires d'inclusion.
option(SFC_ARM7 "lier la traduction du coprocesseur ARM7 (generated7)" OFF)
set(GENERATED7_DIR "${CMAKE_SOURCE_DIR}/../generated7" CACHE PATH "répertoire du C généré du coprocesseur")
if(SFC_ARM7)
  file(GLOB GENERATED7_SOURCES "${GENERATED7_DIR}/*.c")
  if(NOT GENERATED7_SOURCES)
    message(FATAL_ERROR "aucun fichier dans ${GENERATED7_DIR} : lancez tools/recomp/recomp.py --suffixe 7")
  endif()
  add_library(recomp7 STATIC ${GENERATED7_SOURCES})
  target_include_directories(recomp7 PUBLIC "${GENERATED7_DIR}" "${CMAKE_SOURCE_DIR}")
  target_compile_options(recomp7 PRIVATE ${SFC_OPT} -g0 -w -fno-strict-aliasing -pipe)
  if(SFC_COMPTE_TRADUIT)
    target_compile_definitions(recomp7 PRIVATE SFC_COMPTE_TRADUIT)
  endif()
endif()

add_executable(sfc
  main.c rt.c memory.c mmio.c mmio7.c irq.c ipc.c arm7_hle.c arm7_run.c threads7.c sound.c audio.c
  card.c data.c natif.c gx.c gx_geom.c overlays.c threads.c bios.c backup.c verify.c
  video.cpp entre_images.cpp video2d.c attente.c echantillonneur.c apercu.c modifications.c mods.c manette.c
  window_win32.cpp gl_api.cpp)
target_include_directories(sfc PRIVATE "${CMAKE_SOURCE_DIR}" "${GENERATED_DIR}")
# Le runtime — composition des deux écrans, décodage des textures, géométrie, mixage du son —
# est du code à nous, compact, et c'est lui qui consomme la moitié du temps d'une image. Il était
# compilé sans optimisation, le type de construction par défaut étant Debug : l'optimiser ne coûte
# que quelques secondes de compilation, contrairement au code généré qui reste en -O1 vu sa taille.
target_compile_options(sfc PRIVATE -O3 -Wall -Wextra -fno-strict-aliasing)
target_link_libraries(sfc PRIVATE recomp opengl32 gdi32 user32 winmm dinput8 dxguid)
if(SFC_COMPTE_TRADUIT)
  target_compile_definitions(sfc PRIVATE SFC_COMPTE_TRADUIT)
endif()
if(SFC_ARM7)
  target_compile_definitions(sfc PRIVATE SFC_HAVE_ARM7)
  target_include_directories(sfc PRIVATE "${GENERATED7_DIR}")     # natif_auto7.inc, après generated/
  target_link_libraries(sfc PRIVATE recomp7)
endif()

# L'icône de sfc.exe — celle de l'Explorateur et du Gestionnaire des tâches : l'emblème de la cartouche du
# joueur, tiré par tools/icone.py (tools/preparer.py le fait). Le portage ne la livre pas ; sans elle, sfc.exe garde
# l'icône de Windows. La fenêtre du jeu, elle, relit l'emblème dans la cartouche à chaque lancement.
set(SFC_ICONE "" CACHE FILEPATH "icône de sfc.exe (.ico tiré de la cartouche par tools/icone.py)")
if(WIN32 AND SFC_ICONE AND EXISTS "${SFC_ICONE}")
  enable_language(RC)
  file(TO_CMAKE_PATH "${SFC_ICONE}" SFC_ICONE_CHEMIN)
  file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/icone.rc" CONTENT "1 ICON \"${SFC_ICONE_CHEMIN}\"\n")
  target_sources(sfc PRIVATE "${CMAKE_BINARY_DIR}/icone.rc")
endif()

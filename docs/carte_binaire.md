# Carte du binaire — Star Fox Command (Europe)

Générée par `tools/arm9map.py`. Adresses en RAM, telles que le jeu les voit.

## Binaires

| Binaire | RAM | Taille | Entrée |
|---|---|---:|---|
| ARM9 (décompressé) | `0x02000000` | 756344 | `0x02000800` |
| ARM7 | `0x02380000` | 165552 | `0x02380000` |

## Overlays ARM9

Quatre emplacements ; les overlays d'un même emplacement s'excluent.

| Overlay | RAM | Taille | BSS | Init statique | Fichier |
|---:|---|---:|---:|---|---:|
| 0 | `0x0216C440` | 410784 | 58656 | `0x021CBBF0`–`0x021CBC88` | 0 |
| 1 | `0x0216C440` | 134912 | 9984 | `0x0218C0AC`–`0x0218C0B0` | 1 |
| 2 | `0x021DEE00` | 284992 | 4256 | `0x022221BC`–`0x02222200` | 2 |
| 3 | `0x021DEE00` | 237728 | 10464 | `0x02215624`–`0x02215644` | 3 |
| 4 | `0x021DEE00` | 229696 | 30816 | `0x022144B8`–`0x022144E8` | 4 |
| 5 | `0x022257E0` | 151008 | 1792 | `0x022488B4`–`0x022488C0` | 5 |
| 6 | `0x022257E0` | 172480 | 2144 | `0x0224D7E4`–`0x0224D7F0` | 6 |
| 7 | `0x022257E0` | 206240 | 3296 | `0x0225531C`–`0x0225532C` | 7 |
| 8 | `0x022257E0` | 201664 | 2528 | `0x022545E0`–`0x022545EC` | 8 |
| 9 | `0x022257E0` | 206048 | 2432 | `0x0225514C`–`0x02255154` | 9 |
| 10 | `0x022257E0` | 163904 | 3328 | `0x0224B368`–`0x0224B3AC` | 10 |
| 11 | `0x022257E0` | 171264 | 2112 | `0x0224D3AC`–`0x0224D3B8` | 11 |
| 12 | `0x022257E0` | 20608 | 256 | `0x0222A378`–`0x0222A380` | 12 |
| 13 | `0x022257E0` | 145344 | 1472 | `0x02247254`–`0x0224725C` | 13 |
| 14 | `0x022257E0` | 132320 | 1984 | `0x0224463C`–`0x02244658` | 14 |
| 15 | `0x022257E0` | 148832 | 7552 | `0x02247E9C`–`0x02247EB4` | 15 |
| 16 | `0x022257E0` | 204928 | 2720 | `0x022550C0`–`0x022550CC` | 16 |
| 17 | `0x022257E0` | 154304 | 2144 | `0x022493A4`–`0x022493B0` | 17 |
| 18 | `0x022257E0` | 193120 | 2720 | `0x02252308`–`0x02252314` | 18 |
| 19 | `0x022257E0` | 160224 | 2336 | `0x0224AAD0`–`0x0224AADC` | 19 |
| 20 | `0x022257E0` | 143040 | 1408 | `0x022469E0`–`0x022469EC` | 20 |
| 21 | `0x022257E0` | 32 | 0 | `0x022257E0`–`0x022257E4` | 21 |
| 22 | `0x022257E0` | 32 | 0 | `0x022257E0`–`0x022257E4` | 22 |
| 23 | `0x02258A60` | 93696 | 1024 | `0x0226E24C`–`0x0226E260` | 23 |
| 24 | `0x02258A60` | 89632 | 8512 | `0x0226C4C8`–`0x0226C4CC` | 24 |
| 25 | `0x02258A60` | 48544 | 2848 | `0x0226410C`–`0x0226411C` | 25 |

## Registres de commandes et de callbacks

Enregistrements de 12 octets : pointeur vers le nom, pointeur vers la fonction (bit 0 levé pour du Thumb), pointeur de chaînage.


### ARM9 — 82 enregistrements dans 8 table(s)

Table `0x020A3EC4`–`0x020A3F24` (9) :

| Nom | Fonction | Mode |
|---|---|---|
| `CreatePath` | `0x0201CC58` | Thumb |
| `AddPathPos` | `0x0201CAE0` | Thumb |
| `RegistModel` | `0x0201CA60` | Thumb |
| `RegistBillboard` | `0x0201C9E0` | Thumb |
| `RegistModelSet` | `0x0201C968` | Thumb |
| `RegistFlashModelSet` | `0x0201C8F0` | Thumb |
| `RegistModelFromSet` | `0x0201C5F0` | Thumb |
| `CreateFlashModel` | `0x0201C558` | Thumb |
| `CreateLODModel` | `0x0201BF18` | Thumb |

Table `0x020A3F5C`–`0x020A404C` (21) :

| Nom | Fonction | Mode |
|---|---|---|
| `EnableEnemyTemplate` | `0x0201B79C` | Thumb |
| `CreateEnemyTemplate` | `0x0201B470` | Thumb |
| `CreateEnemy` | `0x0201AC48` | Thumb |
| `SetEnemyState` | `0x0201A6CC` | Thumb |
| `IncludeEnemy` | `0x0201A6AC` | Thumb |
| `RegistVolume` | `0x0201A27C` | Thumb |
| `SubVolume` | `0x02019FE8` | Thumb |
| `SubVolumeMM` | `0x02019D1C` | Thumb |
| `VolumeFlags` | `0x02019CD0` | Thumb |
| `Include` | `0x02019CB0` | Thumb |
| `ClearHeightManager` | `0x02019C8C` | Thumb |
| `SetHeightGridSize` | `0x02019C14` | Thumb |
| `SetHeightGridOffset` | `0x02019B40` | Thumb |
| `LoadHeightMap` | `0x02019AF8` | Thumb |
| `RegistHeightModel` | `0x02019AA0` | Thumb |
| `RegistHeightGridObject` | `0x02019838` | Thumb |
| `SetHeightInformer` | `0x02019800` | Thumb |
| `RegistInformerInfinite` | `0x020197F0` | Thumb |
| `CreateBackGround` | `0x020197EC` | Thumb |
| `ClearBackGround` | `0x020197DC` | Thumb |
| `SetBackGroundColor` | `0x02019694` | Thumb |

Table `0x020A4064`–`0x020A40C4` (9) :

| Nom | Fonction | Mode |
|---|---|---|
| `PushBackGround` | `0x020195F0` | Thumb |
| `SetBackGroundRotate` | `0x02019544` | Thumb |
| `SetBackGroundRotateX` | `0x02019498` | Thumb |
| `SetBackGroundRotateZ` | `0x020193EC` | Thumb |
| `SetBackGroundTranslate` | `0x0201921C` | Thumb |
| `SetStageMode` | `0x020191D0` | Thumb |
| `InitBackgroundEffect` | `0x020191C0` | Thumb |
| `SetBackgroundEffect` | `0x020190DC` | Thumb |
| `LoadTexture` | `0x02018F30` | Thumb |

Table `0x020A40DC`–`0x020A4148` (10) :

| Nom | Fonction | Mode |
|---|---|---|
| `CreateTexAnimBase` | `0x02018DE8` | Thumb |
| `CreateTexAnimSeq` | `0x02018B8C` | Thumb |
| `LoadTexAnimPatt` | `0x02018AD8` | Thumb |
| `PushTexAnim` | `0x0201899C` | Thumb |
| `PushTexAnimSeq` | `0x02018840` | Thumb |
| `PushIndexAnimSeq` | `0x02018628` | Thumb |
| `SetTexAnimSize` | `0x02018464` | Thumb |
| `SetTexAnimLoop` | `0x02018428` | Thumb |
| `SetConfigParam` | `0x02018390` | Thumb |
| `LoadAnimSequence` | `0x02018208` | Thumb |

Table `0x020A4160`–`0x020A4184` (4) :

| Nom | Fonction | Mode |
|---|---|---|
| `Version` | `0x02017E4C` | Thumb |
| `RemoteItemBegin` | `0x02017E04` | Thumb |
| `RemoteItemEnd` | `0x02017DC0` | Thumb |
| `BreakPoint` | `0x02017DBC` | Thumb |

Table `0x020A4288`–`0x020A42C4` (6) :

| Nom | Fonction | Mode |
|---|---|---|
| `CreateMarker` | `0x02017978` | Thumb |
| `SetMarkerArg` | `0x0201793C` | Thumb |
| `CreatePulse` | `0x0201783C` | Thumb |
| `PushPulseData` | `0x02017754` | Thumb |
| `SetPulseSinWave` | `0x020175C0` | Thumb |
| `SetPulseRamp` | `0x02017408` | Thumb |

Table `0x020A4440`–`0x020A4524` (20) :

| Nom | Fonction | Mode |
|---|---|---|
| `RegistStageAllClear` | `0x0201DE9C` | Thumb |
| `RegistStage` | `0x0201DD78` | Thumb |
| `LoadStage` | `0x0201DD34` | Thumb |
| `LoadStageDirect` | `0x0201DD30` | Thumb |
| `RegistStageLayer` | `0x0201DB58` | Thumb |
| `SetStageLayer` | `0x0201DAD8` | Thumb |
| `RegistEnemyAllClear` | `0x0201DA30` | Thumb |
| `RegistEnemy` | `0x0201D910` | Thumb |
| `LoadEnemy` | `0x0201D868` | Thumb |
| `RegistNonTarget` | `0x0201D748` | Thumb |
| `LoadStageAllClear` | `0x0201D738` | Thumb |
| `EraseEnemyStore` | `0x0201D734` | Thumb |
| `CreatePlayerDirect` | `0x0201D4BC` | Thumb |
| `CreatePlayer` | `0x0201D14C` | Thumb |
| `ClearPlayerInfo` | `0x0201D140` | Thumb |
| `CreatePlayerFromInfo` | `0x0201D0F0` | Thumb |
| `SetTimeOutTime` | `0x0201D074` | Thumb |
| `SetTimeOutMode` | `0x0201D030` | Thumb |
| `Pause` | `0x0201CFB8` | Thumb |
| `ConfigOpen` | `0x0201CF3C` | Thumb |

Table `0x020ABC04`–`0x020ABC1C` (3) :

| Nom | Fonction | Mode |
|---|---|---|
| `N3abi21__vmi_class_type_infoE` | `0x020ABC18` | ARM |
| `N3abi20__si_class_type_infoE` | `0x020ABC18` | ARM |
| `N3abi17__class_type_infoE` | `0x020ABBF8` | ARM |


### Overlay 0 — 4 enregistrements dans 1 table(s)

Table `0x0219D0B4`–`0x0219D0D8` (4) :

| Nom | Fonction | Mode |
|---|---|---|
| `ContrailScaleX` | `0x021CE60C` | ARM |
| `ContrailScaleZ` | `0x021CE626` | ARM |
| `ContrailResistance` | `0x021CE644` | ARM |
| `ContrailFactorX` | `0x021CE662` | Thumb |


### Overlay 3 — 6 enregistrements dans 2 table(s)

Table `0x021E0DF0`–`0x021E0E08` (3) :

| Nom | Fonction | Mode |
|---|---|---|
| `Strategy_Fog_Color_G` | `0x022157AC` | ARM |
| `Strategy_Fog_Color_B` | `0x022157C2` | Thumb |
| `Strategy_Fog_Color_A` | `0x022157DA` | ARM |

Table `0x021EDB24`–`0x021EDB3C` (3) :

| Nom | Fonction | Mode |
|---|---|---|
| `Strategy_Player_Arwing_Fuel` | `0x02215CE0` | Thumb |
| `Strategy_Player_Arwing_Speed` | `0x02215D02` | Thumb |
| `Strategy_Player_Arwing_BattleTime` | `0x02215D2A` | Thumb |


### Overlay 9 — 7 enregistrements dans 2 table(s)

Table `0x02228EE4`–`0x02228EFC` (3) :

| Nom | Fonction | Mode |
|---|---|---|
| `ZAZAN_2` | `0x02256B20` | ARM |
| `MeshRenderer` | `0x02256B08` | ARM |
| `Slippy` | `0x02256AF8` | ARM |

Table `0x02229358`–`0x0222937C` (4) :

| Nom | Fonction | Mode |
|---|---|---|
| `WolfChatMsg` | `0x02256DC8` | ARM |
| `PantherChatMsg` | `0x02256DB0` | ARM |
| `LeonChatMsg` | `0x02256D9C` | ARM |
| `ZazanChatMsg` | `0x02256D84` | ARM |


### Overlay 14 — 3 enregistrements dans 1 table(s)

Table `0x0224024C`–`0x02240264` (3) :

| Nom | Fonction | Mode |
|---|---|---|
| `Flatland` | `0x022459F4` | ARM |
| `Mountain` | `0x022459FC` | ARM |
| `Fortress` | `0x02245A0C` | Thumb |


### Overlay 23 — 10 enregistrements dans 3 table(s)

Table `0x0225B288`–`0x0225B2AC` (4) :

| Nom | Fonction | Mode |
|---|---|---|
| `overlay_katarina_enemy` | `0x0226E54E` | ARM |
| `overlay_aquas_enemy` | `0x0226E56A` | Thumb |
| `overlay_solar_enemy` | `0x0226E584` | Thumb |
| `overlay_macbeth_enemy` | `0x0226E5A0` | Thumb |

Table `0x0225B63C`–`0x0225B654` (3) :

| Nom | Fonction | Mode |
|---|---|---|
| `overlay_titania_enemy` | `0x0226E5BE` | Thumb |
| `overlay_area6_enemy` | `0x0226E5DA` | Thumb |
| `overlay_venom_enemy` | `0x0226E5F4` | Thumb |

Table `0x0225B930`–`0x0225B948` (3) :

| Nom | Fonction | Mode |
|---|---|---|
| `overlay_boss2_enemy` | `0x0226E626` | Thumb |
| `overlay_extra_enemy` | `0x0226E640` | Thumb |
| `overlay_missile_enemy` | `0x0226E65C` | Thumb |


### Overlay 25 — 3 enregistrements dans 1 table(s)

Table `0x0225A740`–`0x0225A758` (3) :

| Nom | Fonction | Mode |
|---|---|---|
| `loadArchive` | `0x0226435C` | ARM |
| `_table` | `0x02264370` | ARM |
| `couldn't allocate` | `0x02264350` | ARM |


## Résumé

| Binaire | Tables | Enregistrements |
|---|---:|---:|
| ARM9 | 8 | 82 |
| Overlay 0 | 1 | 4 |
| Overlay 1 | 0 | 0 |
| Overlay 2 | 0 | 0 |
| Overlay 3 | 2 | 6 |
| Overlay 4 | 0 | 0 |
| Overlay 5 | 0 | 0 |
| Overlay 6 | 0 | 0 |
| Overlay 7 | 0 | 0 |
| Overlay 8 | 0 | 0 |
| Overlay 9 | 2 | 7 |
| Overlay 10 | 0 | 0 |
| Overlay 11 | 0 | 0 |
| Overlay 12 | 0 | 0 |
| Overlay 13 | 0 | 0 |
| Overlay 14 | 1 | 3 |
| Overlay 15 | 0 | 0 |
| Overlay 16 | 0 | 0 |
| Overlay 17 | 0 | 0 |
| Overlay 18 | 0 | 0 |
| Overlay 19 | 0 | 0 |
| Overlay 20 | 0 | 0 |
| Overlay 21 | 0 | 0 |
| Overlay 22 | 0 | 0 |
| Overlay 23 | 3 | 10 |
| Overlay 24 | 0 | 0 |
| Overlay 25 | 1 | 3 |

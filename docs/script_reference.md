# Référence du langage de script — Star Fox Command

Généré par `tools/analyze_scripts.py` à partir des scripts extraits de la ROM.

- **1405** fichiers de script analysés
- **205** commandes distinctes, **54427** appels

Les scripts sont encodés en **Shift-JIS**. Une commande par ligne ;
`#` introduit un commentaire (fréquemment une ligne désactivée).
Les vecteurs s'écrivent `<x y z>`.

## Commandes

| Commande | Appels | Fichiers | Signature(s) |
|---|---:|---:|---|
| `RegistHeightGridObject` | 13239 | 164 | `IDENT IDENT INT INT` ×13239 |
| `CreateEnemyTemplate` | 4325 | 456 | `IDENT VEC VEC VEC INT` ×4133<br>`IDENT VEC VEC VEC` ×192 |
| `SetConfigParam` | 3265 | 95 | `IDENT INT` ×3043<br>`IDENT IDENT` ×154<br>`IDENT HEX` ×68 |
| `MemberTalks` | 1627 | 48 | `VEC INT` ×877<br>`VEC` ×389<br>`VEC INT INT` ×286 |
| `RegistModel` | 1617 | 213 | `IDENT PATH` ×1616<br>`IDENT IDENT` ×1 |
| `LoadHeightMap` | 1212 | 164 | `IDENT IDENT PATH` ×1212 |
| `CreateObject` | 1208 | 181 | `IDENT IDENT IDENT VEC VEC VEC VEC INT INT IDENT IDENT` ×645<br>`IDENT IDENT IDENT VEC VEC VEC VEC INT INT` ×453<br>`IDENT IDENT IDENT VEC VEC VEC VEC INT INT IDENT IDENT INT` ×60 |
| `SetEnemyState` | 1106 | 188 | `IDENT` ×284<br>`IDENT IDENT IDENT` ×268<br>`IDENT IDENT INT` ×138 |
| `MaskEdit` | 936 | 39 | `IDENT` ×936 |
| `FogEdit` | 936 | 39 | `IDENT` ×936 |
| `SetVolume` | 830 | 13 | `VEC` ×657<br>`VEC VEC` ×97<br>`INT` ×76 |
| `SetModel` | 821 | 13 | `IDENT PATH` ×821 |
| `AddPath` | 684 | 13 | `VEC` ×684 |
| `RegistHeightModel` | 678 | 129 | `IDENT PATH` ×678 |
| `PushTexAnim` | 627 | 35 | `INT FLOAT` ×620<br>`INT IDENT` ×7 |
| `RegistStageLayer` | 616 | 23 | `IDENT IDENT` ×330<br>`IDENT PATH` ×285<br>`IDENT IDENT IDENT` ×1 |
| `LoadTexAnimPatt` | 610 | 35 | `IDENT IDENT` ×610 |
| `SetTexAnimSize` | 607 | 35 | `FLOAT FLOAT` ×600<br>`FLOAT FLOAT FLOAT FLOAT` ×7 |
| `CreateTexAnimBase` | 587 | 35 | `IDENT INT INT` ×387<br>`INT INT` ×200 |
| `SetTexAnimLoop` | 574 | 35 | `IDENT` ×574 |
| `Include` | 560 | 198 | `PATH` ×560 |
| `SetCityState` | 555 | 53 | `IDENT IDENT` ×411<br>`IDENT INT` ×141<br>`IDENT IDENT IDENT IDENT IDENT` ×2 |
| `RegistEnemy` | 510 | 34 | `IDENT PATH` ×260<br>`IDENT` ×250 |
| `break` | 489 | 74 | `(aucun argument)` ×489 |
| `RegistVolume` | 455 | 75 | `IDENT FLOAT` ×204<br>`INT` ×141<br>`IDENT INT` ×49 |
| `SetFlashModel` | 429 | 13 | `(aucun argument)` ×273<br>`VEC` ×151<br>`IDENT` ×5 |
| `RegistRound` | 424 | 60 | `INT` ×417<br>`INT IDENT IDENT` ×4<br>`INT IDENT` ×3 |
| `RegistTexture` | 423 | 222 | `IDENT IDENT VEC IDENT` ×231<br>`IDENT IDENT VEC IDENT IDENT IDENT` ×160<br>`IDENT IDENT VEC IDENT IDENT IDENT IDENT` ×16 |
| `PushPulseData` | 406 | 7 | `INT INT` ×406 |
| `CreateGroup` | 388 | 207 | `IDENT INT VEC` ×334<br>`IDENT` ×18<br>`IDENT INT VEC IDENT IDENT` ×17 |
| `SetWeakPoint` | 370 | 13 | `VEC` ×292<br>`VEC VEC` ×67<br>`INT` ×11 |
| `RegistModelSet` | 356 | 186 | `IDENT PATH` ×356 |
| `RegistStage` | 356 | 42 | `IDENT PATH` ×338<br>`IDENT PATH INT` ×18 |
| `RegistNonTarget` | 331 | 13 | `IDENT PATH` ×331 |
| `CreateDesignButton2` | 329 | 112 | `IDENT IDENT VEC` ×156<br>`IDENT IDENT IDENT VEC VEC` ×82<br>`IDENT IDENT IDENT VEC VEC INT INT IDENT` ×53 |
| `CreateEncountEnemy` | 328 | 53 | `IDENT IDENT INT` ×170<br>`IDENT VEC INT` ×101<br>`IDENT VEC` ×37 |
| `RegistRoundEnd` | 321 | 60 | `(aucun argument)` ×321 |
| `SetEncountState` | 266 | 47 | `IDENT IDENT` ×227<br>`IDENT INT` ×39 |
| `SetPulseRamp` | 259 | 7 | `FLOAT FLOAT FLOAT INT` ×91<br>`FLOAT INT FLOAT INT` ×63<br>`INT FLOAT FLOAT INT` ×35 |
| `SetCallback` | 250 | 13 | `IDENT` ×250 |
| `SetVehicle` | 250 | 13 | `IDENT` ×250 |
| `SetCollisionType` | 250 | 13 | `IDENT` ×250 |
| `SetWeapon` | 250 | 13 | `INT INT IDENT` ×250 |
| `SetExplosion` | 249 | 13 | `INT` ×249 |
| `SetLife` | 248 | 13 | `INT INT INT` ×248 |
| `ActorObject` | 247 | 98 | `IDENT IDENT IDENT IDENT FLOAT FLOAT INT INT INT FLOAT FLOAT FLOAT FLOAT FLOAT FLOAT` ×116<br>`IDENT IDENT IDENT IDENT INT INT FLOAT FLOAT FLOAT INT INT INT INT INT INT` ×43<br>`IDENT IDENT IDENT IDENT FLOAT FLOAT FLOAT FLOAT FLOAT FLOAT FLOAT FLOAT FLOAT FLOAT FLOAT` ×29 |
| `CreateArwing` | 247 | 58 | `IDENT IDENT` ×80<br>`IDENT VEC INT` ×79<br>`IDENT IDENT INT` ×67 |
| `SetRadar` | 233 | 13 | `IDENT` ×233 |
| `SetArwingState` | 230 | 56 | `IDENT IDENT IDENT IDENT IDENT` ×123<br>`IDENT INT` ×77<br>`IDENT IDENT IDENT IDENT IDENT IDENT IDENT IDENT` ×11 |
| `AddTexFlashColor` | 224 | 7 | `INT INT INT` ×224 |
| `PushBackGround` | 218 | 73 | `IDENT` ×218 |
| `SetAnim` | 218 | 12 | `IDENT PATH` ×218 |
| `CreateBoard` | 211 | 141 | `IDENT IDENT VEC VEC INT` ×211 |
| `CreatePulse` | 210 | 7 | `IDENT INT INT` ×210 |
| `SetButtonGraphics` | 208 | 11 | `IDENT IDENT VEC VEC VEC VEC INT INT` ×148<br>`IDENT IDENT VEC VEC VEC VEC INT INT IDENT IDENT` ×45<br>`IDENT IDENT IDENT VEC VEC VEC INT INT` ×9 |
| `RegistEffect` | 203 | 19 | `IDENT` ×203 |
| `SetButtonGraphics2` | 172 | 40 | `VEC IDENT VEC VEC INT` ×93<br>`INT IDENT VEC VEC INT` ×79 |
| `SetPulseSinWave` | 168 | 7 | `FLOAT FLOAT FLOAT INT` ×133<br>`FLOAT FLOAT INT INT` ×21<br>`FLOAT INT INT INT` ×14 |
| `CreateCity` | 165 | 53 | `IDENT IDENT IDENT` ×90<br>`IDENT VEC IDENT` ×73<br>`INT VEC IDENT` ×2 |
| `ClearHeightManager` | 163 | 163 | `(aucun argument)` ×163 |
| `SetHeightGridSize` | 163 | 163 | `INT` ×155<br>`FLOAT` ×8 |
| `PutMessage` | 160 | 58 | `IDENT IDENT IDENT` ×47<br>`IDENT IDENT IDENT IDENT` ×36<br>`IDENT` ×30 |
| `SetInfiniteView` | 156 | 14 | `IDENT` ×156 |
| `SetActorState` | 151 | 38 | `IDENT IDENT IDENT` ×151 |
| `SubVolume` | 147 | 12 | `IDENT INT INT FLOAT FLOAT FLOAT FLOAT` ×100<br>`IDENT FLOAT FLOAT FLOAT FLOAT FLOAT FLOAT` ×16<br>`IDENT FLOAT INT INT FLOAT FLOAT FLOAT` ×14 |
| `RegistPlayer` | 145 | 10 | `IDENT` ×144<br>`(aucun argument)` ×1 |
| `SetTarget` | 136 | 13 | `(aucun argument)` ×136 |
| `CreateNumber` | 130 | 41 | `IDENT IDENT IDENT VEC VEC IDENT VEC VEC INT INT INT IDENT IDENT` ×108<br>`IDENT IDENT IDENT VEC VEC IDENT VEC VEC INT INT INT` ×11<br>`IDENT IDENT IDENT VEC VEC IDENT VEC VEC INT INT INT IDENT IDENT IDENT` ×7 |
| `SetTextureSet` | 130 | 13 | `PATH` ×130 |
| `FinishScenarioPart` | 123 | 48 | `(aucun argument)` ×75<br>`IDENT IDENT` ×48 |
| `RegistWeapon` | 122 | 13 | `IDENT` ×122 |
| `SetSubVolume` | 112 | 9 | `VEC VEC` ×112 |
| `CreateMissile` | 111 | 44 | `IDENT IDENT` ×105<br>`PATH VEC` ×5<br>`IDENT VEC` ×1 |
| `CreateEnemy` | 107 | 38 | `IDENT IDENT IDENT IDENT INT INT INT INT INT INT FLOAT FLOAT FLOAT` ×103<br>`IDENT IDENT IDENT IDENT INT INT INT` ×4 |
| `SetMissionState` | 102 | 58 | `IDENT IDENT` ×96<br>`IDENT IDENT INT` ×3<br>`IDENT` ×2 |
| `SetButtonState` | 102 | 19 | `IDENT IDENT VEC VEC IDENT IDENT` ×74<br>`IDENT IDENT` ×22<br>`IDENT VEC` ×3 |
| `SetBattleMessage` | 99 | 6 | `INT IDENT IDENT IDENT` ×93<br>`INT IDENT IDENT` ×6 |
| `CreateButton` | 99 | 19 | `IDENT IDENT IDENT INT IDENT IDENT` ×47<br>`IDENT IDENT` ×16<br>`IDENT PATH IDENT IDENT` ×14 |
| `RegistTextArea` | 90 | 20 | `IDENT IDENT VEC IDENT` ×78<br>`IDENT IDENT VEC IDENT IDENT IDENT IDENT` ×12 |
| `RegistBillboard` | 89 | 29 | `IDENT PATH` ×89 |
| `SetBillboard` | 88 | 10 | `IDENT PATH` ×88 |
| `RegistPlayerState` | 87 | 8 | `IDENT` ×87 |
| `EnableSkip` | 85 | 38 | `IDENT IDENT IDENT IDENT IDENT` ×81<br>`(aucun argument)` ×4 |
| `DisableSkip` | 85 | 38 | `IDENT IDENT IDENT IDENT IDENT` ×81<br>`(aucun argument)` ×4 |
| `SetModelName` | 79 | 8 | `IDENT` ×79 |
| `SetBoostOnName` | 79 | 8 | `IDENT` ×79 |
| `SetBoostOffName` | 79 | 8 | `IDENT` ×79 |
| `SetBoostEffectName` | 79 | 8 | `IDENT` ×79 |
| `SetWingEffectOffset` | 79 | 8 | `VEC` ×79 |
| `SetSingleLaserOffset` | 79 | 8 | `VEC` ×79 |
| `SetTwinLaserOffset` | 79 | 8 | `VEC` ×79 |
| `SetLaserType` | 79 | 8 | `IDENT` ×79 |
| `SetLockType` | 79 | 8 | `IDENT` ×79 |
| `SetBomb` | 79 | 8 | `INT` ×79 |
| `SetShield` | 79 | 8 | `IDENT` ×79 |
| `SetBoost` | 79 | 8 | `IDENT` ×79 |
| `SetTimeBonus` | 79 | 8 | `IDENT` ×79 |
| `AddPathRot` | 78 | 4 | `FLOAT FLOAT FLOAT FLOAT` ×78 |
| `AddPathPos` | 75 | 5 | `FLOAT FLOAT FLOAT FLOAT` ×55<br>`FLOAT FLOAT INT FLOAT` ×10<br>`FLOAT INT INT FLOAT` ×4 |
| `NextMission` | 75 | 48 | `IDENT INT` ×69<br>`IDENT INT IDENT` ×6 |
| `OpenDebateScreen` | 75 | 29 | `IDENT` ×73<br>`IDENT IDENT` ×2 |
| `CloseDebateScreen` | 74 | 29 | `(aucun argument)` ×74 |
| `ClearBackGround` | 73 | 73 | `(aucun argument)` ×73 |
| `SetBackGroundColor` | 72 | 72 | `INT INT INT` ×72 |
| `CreateMarker` | 71 | 13 | `IDENT VEC VEC VEC` ×71 |
| `MissionInit` | 60 | 60 | `(aucun argument)` ×60 |
| `ZoneInfo` | 59 | 12 | `INT IDENT` ×57<br>`INT IDENT IDENT IDENT` ×1<br>`INT IDENT IDENT` ×1 |
| `SetPathLoop` | 58 | 12 | `IDENT` ×58 |
| `CreateMission` | 58 | 58 | `IDENT INT` ×58 |
| `CreateStrategyMap` | 58 | 58 | `IDENT INT` ×58 |
| `CreateBattleMessage` | 55 | 6 | `IDENT` ×40<br>`IDENT IDENT` ×15 |
| `SetBackgroundEffect` | 52 | 52 | `IDENT FLOAT INT` ×52 |
| `SetCallbackVariable` | 49 | 11 | `IDENT IDENT` ×43<br>`IDENT INT` ×6 |
| `MissileZoneInfo` | 49 | 9 | `INT IDENT` ×49 |
| `Prologue` | 48 | 48 | `IDENT IDENT` ×48 |
| `Epilogue` | 48 | 48 | `IDENT IDENT` ×48 |
| `RegistSound` | 46 | 1 | `IDENT INT` ×46 |
| `CreateLODModel` | 45 | 2 | `IDENT INT INT IDENT IDENT IDENT IDENT` ×45 |
| `CreateDesignButton` | 44 | 11 | `IDENT IDENT IDENT VEC VEC` ×35<br>`IDENT IDENT VEC` ×9 |
| `Page` | 43 | 12 | `INT` ×43 |
| `Image` | 43 | 12 | `IDENT` ×43 |
| `Text` | 43 | 12 | `INT` ×43 |
| `RegistModelFromSet` | 41 | 1 | `IDENT IDENT IDENT` ×41 |
| `VolumeFlags` | 41 | 13 | `IDENT IDENT` ×41 |
| `RemoteItemBegin` | 38 | 38 | `(aucun argument)` ×38 |
| `RemoteItemEnd` | 38 | 38 | `(aucun argument)` ×38 |
| `CreateFogMap` | 38 | 38 | `INT VEC IDENT` ×38 |
| `SetTitleBar` | 30 | 30 | `IDENT INT` ×29<br>`IDENT INT IDENT IDENT` ×1 |
| `CreateTexFlash` | 28 | 7 | `IDENT` ×28 |
| `CreateFlashModel` | 28 | 6 | `IDENT` ×26<br>`IDENT IDENT` ×1<br>`(aucun argument)` ×1 |
| `SubVolumeMM` | 24 | 1 | `IDENT INT INT INT FLOAT INT INT` ×8<br>`IDENT FLOAT INT INT INT INT INT` ×8<br>`IDENT INT FLOAT INT INT INT INT` ×6 |
| `SetSoldier` | 24 | 8 | `(aucun argument)` ×24 |
| `BranchScenario` | 24 | 24 | `IDENT IDENT IDENT IDENT` ×21<br>`IDENT IDENT IDENT IDENT IDENT` ×2<br>`IDENT IDENT` ×1 |
| `AnswerA` | 24 | 24 | `IDENT IDENT` ×23<br>`(aucun argument)` ×1 |
| `AnswerB` | 24 | 24 | `IDENT IDENT` ×22<br>`IDENT IDENT INT` ×1<br>`(aucun argument)` ×1 |
| `EnableHologram` | 24 | 20 | `IDENT` ×16<br>`IDENT IDENT IDENT` ×3<br>`IDENT IDENT` ×2 |
| `CreateTexAnimSeq` | 20 | 11 | `IDENT INT INT IDENT IDENT INT INT` ×17<br>`INT INT IDENT IDENT INT INT` ×3 |
| `PushTexAnimSeq` | 20 | 11 | `INT INT FLOAT` ×20 |
| `RegistMessage` | 20 | 18 | `IDENT IDENT` ×18<br>`IDENT IDENT INT INT INT INT INT INT INT` ×2 |
| `SetMenuFrame` | 20 | 18 | `IDENT VEC VEC` ×20 |
| `SetChildEffect` | 19 | 8 | `IDENT` ×19 |
| `SetButtonTopPos` | 18 | 16 | `VEC` ×18 |
| `SetButtonSpacing` | 18 | 16 | `VEC` ×18 |
| `SetMarkerArg` | 16 | 2 | `IDENT IDENT` ×16 |
| `SetEnemyCount` | 16 | 8 | `INT` ×16 |
| `SetBranchPlanet` | 15 | 15 | `IDENT IDENT` ×13<br>`IDENT IDENT IDENT` ×2 |
| `SetBranchMember` | 14 | 14 | `IDENT IDENT` ×13<br>`IDENT IDENT IDENT` ×1 |
| `CreatePath` | 13 | 5 | `IDENT IDENT IDENT` ×13 |
| `TotalPage` | 12 | 12 | `INT` ×12 |
| `RegistEventMessage` | 10 | 1 | `INT IDENT INT INT INT` ×10 |
| `SetStageMode` | 10 | 10 | `IDENT` ×10 |
| `SetHeightInformer` | 10 | 10 | `IDENT` ×10 |
| `SetItem` | 10 | 10 | `INT` ×10 |
| `RegistStageClear` | 10 | 10 | `(aucun argument)` ×10 |
| `RegistStageLayerClear` | 10 | 10 | `(aucun argument)` ×10 |
| `SetBranchLevel` | 9 | 9 | `IDENT IDENT` ×8<br>`IDENT IDENT IDENT` ×1 |
| `GreatFoxMovesTo` | 8 | 8 | `IDENT` ×6<br>`VEC` ×2 |
| `CreateMissileCapsule` | 8 | 5 | `VEC` ×8 |
| `PushIndexAnimSeq` | 7 | 7 | `INT INT INT INT FLOAT` ×7 |
| `EndSkip` | 7 | 7 | `(aucun argument)` ×7 |
| `startBgm` | 7 | 7 | `INT` ×7 |
| `KeepSkip` | 7 | 7 | `(aucun argument)` ×7 |
| `OverrideMainBG` | 7 | 6 | `IDENT IDENT IDENT` ×3<br>`IDENT` ×3<br>`PATH IDENT` ×1 |
| `CreateArea` | 7 | 7 | `IDENT IDENT IDENT IDENT IDENT` ×7 |
| `SetBackGroundRotate` | 6 | 3 | `INT` ×6 |
| `SetNonEnemy` | 5 | 2 | `(aucun argument)` ×5 |
| `SetMemberWeapon` | 5 | 4 | `IDENT IDENT IDENT` ×5 |
| `CreateTimeCapsule` | 5 | 4 | `VEC` ×5 |
| `EnableEnemyTemplate` | 4 | 4 | `IDENT` ×4 |
| `SetPos` | 4 | 1 | `IDENT INT VEC` ×4 |
| `ResetMainBG` | 4 | 4 | `IDENT` ×4 |
| `SetBackGroundTranslate` | 3 | 3 | `INT INT INT INT` ×3 |
| `setPathLoop` | 3 | 3 | `IDENT` ×3 |
| `AnswerC` | 3 | 3 | `IDENT IDENT` ×3 |
| `CreateShieldCapsule` | 3 | 3 | `VEC` ×3 |
| `RegistCorrectCommand` | 2 | 1 | `INT IDENT` ×2 |
| `CreateRoundCapsule` | 2 | 2 | `VEC` ×2 |
| `SetWaitTime` | 2 | 1 | `FLOAT` ×2 |
| `ScenarioIfNot` | 2 | 1 | `IDENT` ×2 |
| `ScenarioEndIf` | 2 | 1 | `(aucun argument)` ×2 |
| `CreateArwingRefresh` | 2 | 2 | `(aucun argument)` ×2 |
| `LoadAnimSequence` | 2 | 1 | `PATH IDENT IDENT` ×2 |
| `oh` | 1 | 1 | `IDENT IDENT IDENT IDENT IDENT IDENT` ×1 |
| `Version` | 1 | 1 | `IDENT INT IDENT` ×1 |
| `CreateSection` | 1 | 1 | `INT INT INT INT INT INT` ×1 |
| `BeginSection` | 1 | 1 | `INT` ×1 |
| `RegistAnim` | 1 | 1 | `IDENT PATH` ×1 |
| `PushAnim` | 1 | 1 | `IDENT IDENT IDENT` ×1 |
| `CreateBounds` | 1 | 1 | `IDENT IDENT` ×1 |
| `SetBackGroundProjFlag` | 1 | 1 | `IDENT` ×1 |
| `SetBackGroundScale` | 1 | 1 | `FLOAT FLOAT FLOAT` ×1 |
| `SetBackGroundZHack` | 1 | 1 | `FLOAT` ×1 |
| `SetBackgroundColor` | 1 | 1 | `INT INT INT` ×1 |
| `StartSetup` | 1 | 1 | `IDENT` ×1 |
| `LoadMesh` | 1 | 1 | `PATH` ×1 |
| `LoadAnim` | 1 | 1 | `PATH` ×1 |
| `EndSetup` | 1 | 1 | `(aucun argument)` ×1 |
| `CreateCamera` | 1 | 1 | `INT FLOAT` ×1 |
| `SetCoordinate` | 1 | 1 | `IDENT INT IDENT IDENT INT FLOAT` ×1 |
| `LookAtThing` | 1 | 1 | `IDENT INT IDENT INT VEC FLOAT` ×1 |
| `LabelStageSelectNext` | 1 | 1 | `(aucun argument)` ×1 |
| `AddWeakPoint` | 1 | 1 | `VEC VEC` ×1 |
| `AnswerD` | 1 | 1 | `IDENT IDENT` ×1 |
| `LabelCorneriaNext` | 1 | 1 | `(aucun argument)` ×1 |
| `f` | 1 | 1 | `(aucun argument)` ×1 |

## Exemples par commande

### `RegistHeightGridObject`

```
RegistHeightGridObject HeightPoly Pillar      1 1
RegistHeightGridObject HeightPoly F           2 1
RegistHeightGridObject HeightPoly H           3 1
```

### `CreateEnemyTemplate`

```
CreateEnemyTemplate eAngelFish <0 850 3500> <0 0 0> <1 1 1> 0
CreateEnemyTemplate eAngelFish <0 850 -3500> <0 0 0> <1 1 1> 0
CreateEnemyTemplate eAngelFish <3500 850 0> <0 0 0> <1 1 1> 0
```

### `SetConfigParam`

```
SetConfigParam MaxHeight 1000
SetConfigParam MinHeight 70
SetConfigParam Camera_Height 0
```

### `MemberTalks`

```
MemberTalks < left naus >
MemberTalks < right fox > 0724
MemberTalks < left none >
```

### `RegistModel`

```
RegistModel ArwingShield rom:/ArwingShield.nsbmd
RegistModel B rom:/B.nsbmd
RegistModel E rom:/E.nsbmd
```

### `LoadHeightMap`

```
LoadHeightMap Pillar      Convex  /3D/Venom/GrndSetA/Pillar_Col.hgt
LoadHeightMap C           Convex  /3D/Venom/GrndSetA/C_Col.hgt
LoadHeightMap H           Convex  /3D/Venom/GrndSetA/H_Col.hgt
```

### `CreateObject`

```
CreateObject None info_board msg_tex <0 0> <232 81> <0 0> <1 1> 31 15
CreateObject None handicap_board text_tex <0 18> <230 13> <1 0> <1 1> 31 16			# “HANDICAP”
CreateObject None handicap_board text_tex <0 34> <209 20> <11 25> <1 1> 31 16		# ボタン枠
```

### `SetEnemyState`

```
SetEnemyState DemoEnemy
SetEnemyState DemoEnemy
SetEnemyState DemoEnemy
```

### `MaskEdit`

```
MaskEdit ................................
MaskEdit .n.....................L........
MaskEdit .........l......T...........h...
```

### `FogEdit`

```
FogEdit XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
FogEdit XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
FogEdit XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
```

### `SetVolume`

```
SetVolume <100>
SetVolume <50>
SetVolume <250>
```

### `SetModel`

```
SetModel DummyTargetEnemy /3D/Enemy/DummyCube/DummyCubeS.mdl
SetModel StarFish /3D/Enemy/StarFish/StarFish.mdl
SetModel StarFish_Demo /3D/Enemy/StarFish/StarFish.mdl
```

### `AddPath`

```
AddPath <2500 600 2500>
AddPath <-2500 600 2500>
AddPath <-2500 600 -2500>
```

### `RegistHeightModel`

```
RegistHeightModel Pillar       /3D/Venom/GrndSetA/Pillar.mdl
RegistHeightModel PillarGR_Gr  /3D/Venom/GrndSetA/PillarGR_Gr.mdl
RegistHeightModel C            /3D/Venom/GrndSetA/C.mdl
```

### `PushTexAnim`

```
PushTexAnim 0 1.0
PushTexAnim 0 1.0
PushTexAnim 0 1.0
```

### `RegistStageLayer`

```
RegistStageLayer _DummyStage_1		_DummyStage_1
RegistStageLayer _DummyStage_2		_DummyStage_2
RegistStageLayer _DummyStage_3		_DummyStage_3
```

### `LoadTexAnimPatt`

```
LoadTexAnimPatt PLTT4 rom:/3D/Textures/Overlays/HUD/NumLG_SP
LoadTexAnimPatt PLTT4 rom:/3D/Textures/Overlays/HUD/0G_SP
LoadTexAnimPatt PLTT4 rom:/3D/Textures/Overlays/HUD/alert_number
```

### `SetTexAnimSize`

```
SetTexAnimSize 64.0 64.0
SetTexAnimSize 128.0 16.0
SetTexAnimSize 16.0 16.0
```

### `CreateTexAnimBase`

```
CreateTexAnimBase NumLG 128 16
CreateTexAnimBase 0G 16 16
CreateTexAnimBase AlertNum 256 128
```

### `SetTexAnimLoop`

```
SetTexAnimLoop true
SetTexAnimLoop true
SetTexAnimLoop true
```

### `Include`

```
Include rom:/CollisionChanges.txt
Include rom:/tankpath.txt
Include rom:/HoverPath1.txt
```

### `SetCityState`

```
SetCityState model_name CityBase
SetCityState enemy_name HoverEnemy
SetCityState charge 3
```

### `RegistEnemy`

```
RegistEnemy RollingTank rom:/3D/Corneria/Scripts/RollingTank.txt
RegistEnemy DevilWorm rom:/3D/Corneria/Scripts/DevilWorm.txt
RegistEnemy LaserTurret rom:/3D/Corneria/Scripts/LaserTurret.txt
```

### `break`

```
break
break
break
```

### `RegistVolume`

```
RegistVolume Laser 15.0
RegistVolume TankMissile 15.0
RegistVolume Enemy01 40.0
```

### `SetFlashModel`

```
SetFlashModel <31 0 0>
SetFlashModel <31 0 0>
SetFlashModel <31 0 0>
```

### `RegistRound`

```
RegistRound 0
RegistRound 1
RegistRound 2
```

### `RegistTexture`

```
RegistTexture msg_tex Menu/MultiPlay/mp_err_cl_txt <256 128> COMP4x4
RegistTexture text_tex	Menu/MultiPlay/handicap_d_txt2 <256 256> COMP4x4		#
RegistTexture text_tex	Menu/MultiPlay/handicap_c_txt2 <256 128> COMP4x4
```

### `PushPulseData`

```
PushPulseData 15 15
PushPulseData 15 15
PushPulseData 15 15
```

### `CreateGroup`

```
CreateGroup board_group 16 <12 50>
CreateGroup global_group 17 <0 0>
CreateGroup board_group 17 <12 21>
```

### `SetWeakPoint`

```
SetWeakPoint <100>
SetWeakPoint <50>
SetWeakPoint <50>
```

### `RegistModelSet`

```
RegistModelSet CorneriaSea rom:/CorneriaSea_notex.nsbmd
RegistModelSet CorneriaBlock rom:/CorneriaLod3_notex.nsbmd
RegistModelSet ArwingSet rom:/3D/Starfox/Arwing/Arwing.tset
```

### `RegistStage`

```
RegistStage CorneriaCityEBase rom:/3D/Corneria/Scripts/CorCityB.txt
RegistStage CorneriaCity rom:/3D/Corneria/Scripts/CorCity.txt
RegistStage CorneriaCitySFBase rom:/3D/Corneria/Scripts/CorCityB.txt
```

### `RegistNonTarget`

```
RegistNonTarget _DummyStage_1		/3D/Shared/Scripts/gDummyStage_1.txt
RegistNonTarget _DummyStage_2		/3D/Shared/Scripts/gDummyStage_2.txt
RegistNonTarget _DummyStage_3		/3D/Shared/Scripts/gDummyStage_3.txt
```

### `CreateDesignButton2`

```
CreateDesignButton2 Custom2 btn_handi_low board_group <69 18> <12 26> 31 15 no_shadow
CreateDesignButton2 Custom2 btn_handi_middle board_group <69 18> <81 26> 31 15 no_shadow
CreateDesignButton2 Custom2 btn_handi_high board_group <69 18> <150 26> 31 15 no_shadow
```

### `CreateEncountEnemy`

```
CreateEncountEnemy HoverEnemy <127 78>
CreateEncountEnemy ChaseEnemy <230 70>
CreateEncountEnemy HoverEnemy AnyCity
```

### `RegistRoundEnd`

```
RegistRoundEnd
RegistRoundEnd
RegistRoundEnd
```

### `SetEncountState`

```
SetEncountState type normal:3
SetEncountState type normal:3
SetEncountState type normal:3
```

### `SetPulseRamp`

```
SetPulseRamp 0.1 1.0 0.15 0
SetPulseRamp 0.2 1.0 0.3 0
SetPulseRamp 0.2 1.0 0.2 0
```

### `SetCallback`

```
SetCallback TestCallback
SetCallback ecStarfish
SetCallback ecPorcupineFish
```

### `SetVehicle`

```
SetVehicle SimpleVehicle
SetVehicle CollideVehicle
SetVehicle CollideVehicle
```

### `SetCollisionType`

```
SetCollisionType ENEMY
SetCollisionType ENEMY
SetCollisionType ENEMY
```

### `SetWeapon`

```
SetWeapon 0 10 RegularLaser
SetWeapon 0 15 RingLaserA
SetWeapon 0 15 SquareLaser
```

### `SetExplosion`

```
SetExplosion 0
SetExplosion 0
SetExplosion 0
```

### `SetLife`

```
SetLife 50 10 0
SetLife 100 10 0
SetLife 200 10 0
```

### `ActorObject`

```
ActorObject Dummy RedStarItem CollideVehicle MAP_OBJECT  0 0 0.0 0.0 0.0 0 0 0 1 1 1
ActorObject Dummy RedStarItem CollideVehicle MAP_OBJECT  0 0 0.0 0.0 0.0 0 0 0 1 1 1
ActorObject Dummy RedStarItem CollideVehicle MAP_OBJECT  0 0 0.0 0.0 0.0 0 0 0 1 1 1
```

### `CreateArwing`

```
CreateArwing naus <21 168> 45
CreateArwing fox <29 146> 30
CreateArwing slippy <43 160> 60
```

### `SetRadar`

```
SetRadar FLYING_ENEMY_TARGET
SetRadar FLYING_ENEMY_TARGET
SetRadar FLYING_ENEMY_TARGET
```

### `SetArwingState`

```
SetArwingState distance 96
SetArwingState distance 128
SetArwingState distance 128
```

### `AddTexFlashColor`

```
AddTexFlashColor 31 3 0
AddTexFlashColor 31 3 0
AddTexFlashColor 31 3 0
```

### `PushBackGround`

```
PushBackGround BG
PushBackGround Clouds
PushBackGround Sun
```

### `SetAnim`

```
SetAnim Open /3D/Enemy/StarFish/Open.anim
SetAnim Close /3D/Enemy/StarFish/Close.anim
SetAnim StarFishAnim_Demo /3D/Enemy/StarFish/StarFishDemo.anim
```

### `CreateBoard`

```
CreateBoard info_board board_group <232 81> <0 0> 16
CreateBoard handicap_board board_group <232 57> <0 0> 17
CreateBoard control_board board_group <232 57> <0 71> 17
```

### `CreatePulse`

```
CreatePulse SmallDamage 15 0
CreatePulse LargeDamage 25 0
CreatePulse NearBlast 50 0
```

### `SetButtonGraphics`

```
SetButtonGraphics Normal button_wnd_tex <0 0> <7 23> <0 0> <1 1> 31 16			# ボタン枠（３つ）
SetButtonGraphics Normal button_wnd_tex <8 0> <1 23> <7 0> <115 1> 31 16
SetButtonGraphics Normal button_wnd_tex <10 0> <5 23> <122 0> <1 1> 31 16
```

### `RegistEffect`

```
RegistEffect ShieldG_S
RegistEffect ShieldG_M
RegistEffect ShieldG_L
```

### `SetButtonGraphics2`

```
SetButtonGraphics2 <0 0> text_tex <1 56> <69 18> 14
SetButtonGraphics2 <0 0> text_tex <70 56> <69 18> 14
SetButtonGraphics2 <0 0> text_tex <139 56> <69 18> 14
```

### `SetPulseSinWave`

```
SetPulseSinWave 0.1 0.9 1.0 5
SetPulseSinWave 0.1 0.9 1.0 5
SetPulseSinWave 0.1 0.9 1 5
```

### `CreateCity`

```
CreateCity Coal <45 97> enemy
CreateCity Nest <109 35> enemy
CreateCity Earth <204 18> enemy
```

### `ClearHeightManager`

```
ClearHeightManager
ClearHeightManager
ClearHeightManager
```

### `SetHeightGridSize`

```
SetHeightGridSize 2560
SetHeightGridSize 2560
SetHeightGridSize 2560
```

### `PutMessage`

```
PutMessage slippy:179 naus:180 fox:181 slippy:182 fox:183 naus:184
PutMessage fox:185 slippy:186 187 fox:188 slippy:189 naus:190 fox:191
PutMessage NAUS_SETUMEI
```

### `SetInfiniteView`

```
SetInfiniteView TRUE
SetInfiniteView TRUE
SetInfiniteView TRUE
```

### `SetActorState`

```
SetActorState CallbackVariable Laser Twin
SetActorState CallbackVariable Bomb Smart
SetActorState CallbackVariable Laser Plasma
```

### `SubVolume`

```
SubVolume BoltCopter 0 0 0 1 1 1
SubVolume Arwing 0 0 0 0.125 0.125 0.125
SubVolume Arwing 0.9375 0 0 0.0625 0.0625 0.0625
```

### `RegistPlayer`

```
RegistPlayer fox
RegistPlayer krystal
RegistPlayer falco
```

### `SetTarget`

```
SetTarget
SetTarget
SetTarget
```

### `CreateNumber`

```
CreateNumber win_1 board_1 num_tex <33 11> <9 13> right <118 4> <27 13> 0 31 15			# Win数
CreateNumber win_2 board_2 num_tex <33 11> <9 13> right <118 4> <27 13> 0 31 15			# Win数
CreateNumber win_3 board_3 num_tex <33 11> <9 13> right <118 4> <27 13> 0 31 15			# Win数
```

### `SetTextureSet`

```
SetTextureSet /3D/Enemy/PorcupineFish/PorcupineFish.tset
SetTextureSet /3D/Enemy/MantaRay/MantaRay.tset
SetTextureSet /3D/Enemy/AstMine/AstMine.tset
```

### `FinishScenarioPart`

```
FinishScenarioPart	# ストラテジーマップへ
FinishScenarioPart
FinishScenarioPart
```

### `RegistWeapon`

```
RegistWeapon RegularLaser
RegistWeapon Beam
RegistWeapon RingLaserA
```

### `SetSubVolume`

```
SetSubVolume <0 0 -0.3> <0.2 0.2 0.2>
SetSubVolume <0 0 0.3> <0.2 0.2 0.2>
SetSubVolume <0 0 0> <0.2 0.2 0.2>
```

### `CreateMissile`

```
CreateMissile eMissileSimple Boss
CreateMissile /3D/Venom/Scripts/eLastAngler.txt <128 0>
CreateMissile eMissileSimple AnyCity
```

### `CreateEnemy`

```
CreateEnemy TriggerGate SeedTrigger BasicFlyVehicle ITEM_OBJECT 0 1000 0 0 90 0     1.5 1.5 1.5
CreateEnemy TriggerGate SeedTrigger BasicFlyVehicle ITEM_OBJECT -3800 500 -3800 0 0 0     1.5 1.5 1.5
CreateEnemy TriggerGate SeedTrigger BasicFlyVehicle ITEM_OBJECT -700 500 0 0 90 0     1.5 1.5 1.5
```

### `SetMissionState`

```
SetMissionState ClearRule GetAllBase
SetMissionState ClearRule DestroyTargetEnemy 1
SetMissionState ClearRule GetAllBase
```

### `SetButtonState`

```
SetButtonState set_texture TextureMain <0 112> <96 16>		# ボタンにテクスチャの設定
SetButtonState set_texture TextureMain <0 112> <96 16>		# ボタンにテクスチャの設定
SetButtonState set_texture TextureMain <0 112> <96 16>		# ボタンにテクスチャの設定
```

### `SetBattleMessage`

```
SetBattleMessage 20521 oikonny SHORT HIGH
SetBattleMessage 20522 oikonny SHORT HIGH
SetBattleMessage 20523 oikonny SHORT
```

### `CreateButton`

```
CreateButton ButtonTest1 function:StartDebugScene		# ボタンの作成
CreateButton ButtonTest2 function:StartDebugScene Challange
CreateButton ButtonTest3 function:StartAdvatise Prologue 1
```

### `RegistTextArea`

```
RegistTextArea name_tex_1 Menu/Shared/net_name_1 <64 16> PLTT4
RegistTextArea name_tex_2 Menu/Shared/net_name_2 <64 16> PLTT4
RegistTextArea name_tex_3 Menu/Shared/net_name_3 <64 16> PLTT4
```

### `RegistBillboard`

```
RegistBillboard MP_Yellow_Ring /3D/Starfox/Item/Yellow_Ring.mdl
RegistBillboard Yellow_Ring /3D/Starfox/Item/Yellow_Ring.mdl
RegistBillboard Atom /3D/Starfox/Item/AtomG.mdl
```

### `SetBillboard`

```
SetBillboard Bubble /3D/Enemy/Weapon/Bubble.mdl
SetBillboard BubbleBurst /3D/Enemy/Weapon/Burst.mdl
SetBillboard MijinkoJoint /3D/Enemy/Mijinko/Ball.mdl
```

### `RegistPlayerState`

```
RegistPlayerState		naus
RegistPlayerState		fox
RegistPlayerState		krystal
```

### `EnableSkip`

```
EnableSkip	# skip command 1. ここから先、省略可能とする
EnableSkip	# skip command 1. ここから先、省略可能とする
EnableSkip	# skip command 1. ここから先、省略可能とする
```

### `DisableSkip`

```
DisableSkip	# skip command 2. ここから先、省略禁止。また、スキップする事が出来るのもここまで
DisableSkip	# skip command 2. ここから先、省略禁止。また、スキップする事が出来るのもここまで
DisableSkip	# skip command 2. ここから先、省略禁止。また、スキップする事が出来るのもここまで
```

### `SetModelName`

```
SetModelName			FoxArwing
SetModelName			KrystalArwing
SetModelName			FalcoArwing
```

### `SetBoostOnName`

```
SetBoostOnName			FoxArwingOn
SetBoostOnName			KrystalArwingOn
SetBoostOnName			FalcoArwingOn
```

### `SetBoostOffName`

```
SetBoostOffName			FoxArwingOff
SetBoostOffName			KrystalArwingOff
SetBoostOffName			FalcoArwingOff
```

### `SetBoostEffectName`

```
SetBoostEffectName		FoxArwingBoost
SetBoostEffectName		KrystalArwingBoost
SetBoostEffectName		FalcoArwingBoost
```

### `SetWingEffectOffset`

```
SetWingEffectOffset		<2.6 -0.092 -3.042>
SetWingEffectOffset		<3.138 -0.381 0.674>
SetWingEffectOffset		<4.154 -0.686 0.322>
```

### `SetSingleLaserOffset`

```
SetSingleLaserOffset	<0 -0.02 3.259>
SetSingleLaserOffset	<0.034 -0.071 1.737>
SetSingleLaserOffset	<0 0 2.66>
```

### `SetTwinLaserOffset`

```
SetTwinLaserOffset		<1.945 -0.049 -1.593>
SetTwinLaserOffset		<1.544 -0.324 1.691>
SetTwinLaserOffset		<1.312 0.018 1.218>
```

### `SetLaserType`

```
SetLaserType			Single
SetLaserType			Twin
SetLaserType			Single
```

### `SetLockType`

```
SetLockType				Single
SetLockType				Single
SetLockType				Multi
```

### `SetBomb`

```
SetBomb					2
SetBomb					1
SetBomb					1
```

### `SetShield`

```
SetShield				Middle
SetShield				Middle
SetShield				Small
```

### `SetBoost`

```
SetBoost				Large
SetBoost				Middle
SetBoost				Large
```

### `SetTimeBonus`

```
SetTimeBonus			Disable
SetTimeBonus			Disable
SetTimeBonus			Enable
```

### `AddPathRot`

```
AddPathRot 	2.0 	0.0	0.0	0.0
AddPathRot 	1.0 	0.0	90.0	0.0
AddPathRot 	2.0 	0.0	90.0	0.0
```

### `AddPathPos`

```
AddPathPos 6.5 0 0 0
AddPathPos 6.5 0 0 531.69677
AddPathPos 6.5 8.95183 0 1062.98632
```

### `NextMission`

```
NextMission Venom 0
NextMission Sector 0
NextMission Ending 7   #end
```

### `OpenDebateScreen`

```
OpenDebateScreen Menu/SPart/d_machine
OpenDebateScreen Menu/SPart/data_venom_sea
OpenDebateScreen Menu/SPart/data_venom_sea
```

### `CloseDebateScreen`

```
CloseDebateScreen
CloseDebateScreen
CloseDebateScreen
```

### `ClearBackGround`

```
ClearBackGround
ClearBackGround
ClearBackGround
```

### `SetBackGroundColor`

```
SetBackGroundColor 0 0 0
SetBackGroundColor 0 0 0
SetBackGroundColor 0 0 0
```

### `CreateMarker`

```
CreateMarker mSemirossiaHand <-389.1058 449.1505 -2385.436> <0 88 0> <1 1 1>
CreateMarker mSemirossiaHand <-400.6726 471.6853 2071.523> <0 88 0> <1 1 1>
CreateMarker mSemirossiaHand <-400.0113 449.1505 -3045.578> <0 88 0> <1 1 1>
```

### `MissionInit`

```
MissionInit
MissionInit
MissionInit
```

### `ZoneInfo`

```
ZoneInfo 1 Trench
ZoneInfo 2 Flatland
ZoneInfo 3 Mountain
```

### `SetPathLoop`

```
SetPathLoop true
SetPathLoop true
SetPathLoop true
```

### `CreateMission`

```
CreateMission Corneria 3
CreateMission Venom 99
CreateMission Venom 4
```

### `CreateStrategyMap`

```
CreateStrategyMap Corneria 1
CreateStrategyMap Venom 0
CreateStrategyMap Venom 1
```

### `CreateBattleMessage`

```
CreateBattleMessage Start
CreateBattleMessage Destroy
CreateBattleMessage DestroyParts
```

### `SetBackgroundEffect`

```
SetBackgroundEffect Stars 500.0 4
SetBackgroundEffect Snow 900.0 4
SetBackgroundEffect Snow 900.0 4
```

### `SetCallbackVariable`

```
SetCallbackVariable ATTACK Bubble
SetCallbackVariable TYPE BOSS
SetCallbackVariable WOLFEN_PILOT SLIPPY
```

### `MissileZoneInfo`

```
MissileZoneInfo 1 Trench_Gr
MissileZoneInfo 2 Flatland_Gr
MissileZoneInfo 3 Mountain_Gr
```

### `Prologue`

```
Prologue	# ミッション開始位置
Prologue	# ミッション開始位置
Prologue	# ミッション開始位置
```

### `Epilogue`

```
Epilogue	# ミッションクリア後
Epilogue	# ミッションクリア後
Epilogue	# ミッションクリア後
```

### `RegistSound`

```
RegistSound SE_Laser 1
RegistSound SE_Hit_Laser 1
RegistSound SE_Explosion 1
```

### `CreateLODModel`

```
CreateLODModel BK01 704 4000 BK01_LodA BK01_LodA BK01_LodC BK01_LodC
CreateLODModel BK02 704 4000 BK02_LodA BK02_LodA BK02_LodC BK02_LodC
CreateLODModel BK03 704 4000 BK03_LodA BK03_LodA BK03_LodC BK03_LodC
```

### `CreateDesignButton`

```
CreateDesignButton Custom restart_button button_group <123 19> <0 0>
CreateDesignButton Custom gemeselect_button button_group <123 19> <0 35>
CreateDesignButton Custom title_button button_group <123 19> <0 60>
```

### `Page`

```
Page 1
Page 2
Page 3
```

### `Image`

```
Image	Adv/Ending/As4_end_01
Image	Adv/Ending/As4_end_02
Image	Adv/Ending/As4_end_03
```

### `Text`

```
Text	32036
Text	32037
Text	32038
```

### `RegistModelFromSet`

```
RegistModelFromSet SeaA SeaA CorneriaSea
RegistModelFromSet SeaB SeaB CorneriaSea
RegistModelFromSet SeaC SeaC CorneriaSea
```

### `VolumeFlags`

```
VolumeFlags Arwing RotRelative
VolumeFlags Spider RotRelative
VolumeFlags BoltSpark RotRelative
```

### `RemoteItemBegin`

```
RemoteItemBegin
RemoteItemBegin
RemoteItemBegin
```

### `RemoteItemEnd`

```
RemoteItemEnd
RemoteItemEnd
RemoteItemEnd
```

### `CreateFogMap`

```
CreateFogMap 0 <31 22 0 128> strong
CreateFogMap 0 <31 22 0 128> strong
CreateFogMap 0 <31 22 0 128> strong
```

### `SetTitleBar`

```
SetTitleBar Menu/Gallery/end_gallery_tl2 210
SetTitleBar Menu/Gallery/pilot_tl2 210
SetTitleBar Menu/Gallery/pilot_tl2 210
```

### `CreateTexFlash`

```
CreateTexFlash Flash0
CreateTexFlash Flash1
CreateTexFlash Flash2
```

### `CreateFlashModel`

```
CreateFlashModel DoorL_LodA
CreateFlashModel DoorL_LodB
CreateFlashModel DoorL_LodC
```

### `SubVolumeMM`

```
SubVolumeMM GimickB -1 -1 -1 1 -0.8 1
SubVolumeMM GimickB -1 -1 -1 -0.8 1 1
SubVolumeMM GimickB 0.8 -1 -1 1 1 1
```

### `SetSoldier`

```
SetSoldier
SetSoldier
SetSoldier
```

### `BranchScenario`

```
BranchScenario AnswerA:742 AnswerB:743	# 分岐
BranchScenario AnswerA:2850 AnswerB:2851	# 分岐
BranchScenario AnswerA:742 AnswerB:743	# 分岐
```

### `AnswerA`

```
AnswerA	# 分岐１
AnswerA	# 分岐１
AnswerA	# 分岐１
```

### `AnswerB`

```
AnswerB	# 分岐２
AnswerB	# 分岐 2
AnswerB	# 分岐２
```

### `EnableHologram`

```
EnableHologram wolf
EnableHologram ash
EnableHologram peppy
```

### `CreateTexAnimSeq`

```
CreateTexAnimSeq ExpTest 64 64 PLTT16 rom:/3D/Textures/FX/Explosion/exp_test0 8 0
CreateTexAnimSeq Explosion 64 64 COMP4x4 rom:/3D/Textures/FX/Explosion/ExA_SP 16 1
CreateTexAnimSeq PlasmaBall 32 32 A3I5 rom:/3D/Textures/FX/EnemyFX/Charge 8 1
```

### `PushTexAnimSeq`

```
PushTexAnimSeq 0 8 0.03
PushTexAnimSeq 0 16 0.03
PushTexAnimSeq 0 8 0.03
```

### `RegistMessage`

```
RegistMessage NAUS_SETUMEI naus:171 172 173 174 175 176 177 178
RegistMessage BARRIER_OPEN naus:245
RegistMessage MESSAGE_INTRO naus:171
```

### `SetMenuFrame`

```
SetMenuFrame TextureMain <0 0> <96 16>
SetMenuFrame TextureMain <0 0> <96 16>
SetMenuFrame TextureMain <0 0> <128 16>
```

### `SetChildEffect`

```
SetChildEffect ElecBeam
SetChildEffect EnergyCharge
SetChildEffect Sand
```

### `SetButtonTopPos`

```
SetButtonTopPos <78 42>
SetButtonTopPos <78 30>
SetButtonTopPos <78 42>
```

### `SetButtonSpacing`

```
SetButtonSpacing <0 27>
SetButtonSpacing <0 24>
SetButtonSpacing <0 27>
```

### `SetMarkerArg`

```
SetMarkerArg Hand R
SetMarkerArg Hand L
SetMarkerArg Hand R
```

### `SetEnemyCount`

```
SetEnemyCount 4
SetEnemyCount 3
SetEnemyCount 3
```

### `SetBranchPlanet`

```
SetBranchPlanet Corneria Corneria
SetBranchPlanet Venom SectorY
SetBranchPlanet Titania Titania
```

### `SetBranchMember`

```
SetBranchMember fox fox
SetBranchMember wolf fox
SetBranchMember wolf wolf
```

### `CreatePath`

```
CreatePath FlashBomberPath true true
CreatePath HoverPath1 false false
CreatePath TestPath true true
```

### `TotalPage`

```
TotalPage 3
TotalPage 3
TotalPage 3
```

### `RegistEventMessage`

```
RegistEventMessage 1 CFDAE 0 1 500
RegistEventMessage 2 CDA 1 1 500
RegistEventMessage 3 EDH 0 4 9000
```

### `SetStageMode`

```
SetStageMode Scroll
SetStageMode Scroll
SetStageMode Scroll
```

### `SetHeightInformer`

```
SetHeightInformer Linear
SetHeightInformer Linear
SetHeightInformer Linear
```

### `SetItem`

```
SetItem 0
SetItem 0
SetItem 0
```

### `RegistStageClear`

```
RegistStageClear
RegistStageClear
RegistStageClear
```

### `RegistStageLayerClear`

```
RegistStageLayerClear
RegistStageLayerClear
RegistStageLayerClear
```

### `SetBranchLevel`

```
SetBranchLevel none none
SetBranchLevel none none
SetBranchLevel none gold
```

### `GreatFoxMovesTo`

```
GreatFoxMovesTo Corneria
GreatFoxMovesTo Corneria
GreatFoxMovesTo Corneria
```

### `CreateMissileCapsule`

```
CreateMissileCapsule <200 160>
CreateMissileCapsule <90 116>
CreateMissileCapsule <138 20>
```

### `PushIndexAnimSeq`

```
PushIndexAnimSeq 64 64 0 16 0.03
PushIndexAnimSeq 64 64 0 16 0.03
PushIndexAnimSeq 64 64 0 16 0.03
```

### `EndSkip`

```
EndSkip
EndSkip
EndSkip
```

### `startBgm`

```
startBgm 1
startBgm 1
startBgm 1
```

### `KeepSkip`

```
KeepSkip
KeepSkip
KeepSkip
```

### `OverrideMainBG`

```
OverrideMainBG Menu/SPart/EnemyBG1       #下画面の背景を変更する。会話中に何度も変更出来るようにするため、画像形式は固定。4x4. 256x128.
OverrideMainBG Menu/SPart/EnemyBG1#下画面の背景を変更する。会話中に何度も変更出来るようにするため、画像形式は固定。4x4. 256x128.
OverrideMainBG Menu/SPart/EnemyBG1       #下画面の背景を変更する。会話中に何度も変更出来るようにするため、画像形式は固定。4x4. 256x128.
```

### `CreateArea`

```
CreateArea heavy [ zone:3 ] move
CreateArea heavy [ zone:6 ] move
CreateArea heavy [ zone:9 ] move
```

### `SetBackGroundRotate`

```
SetBackGroundRotate 11
SetBackGroundRotate -17
SetBackGroundRotate -3
```

### `SetNonEnemy`

```
SetNonEnemy
SetNonEnemy
SetNonEnemy
```

### `SetMemberWeapon`

```
SetMemberWeapon fox Laser Plasma
SetMemberWeapon fox Laser Twin
SetMemberWeapon fox Laser Twin
```

### `CreateTimeCapsule`

```
CreateTimeCapsule <70 65>
CreateTimeCapsule <169 139>
CreateTimeCapsule <142 104>
```

### `EnableEnemyTemplate`

```
EnableEnemyTemplate eLimbo
EnableEnemyTemplate eBakula
EnableEnemyTemplate eLimbo
```

### `SetPos`

```
SetPos PLAYER 0 <0.0 800.0 0.0>
SetPos CAMERA 0 <8.0 0.0 35.0>
SetPos OBJECT 0 <0.0 800.0 0.0>
```

### `ResetMainBG`

```
ResetMainBG     #下画面の背景を戻す
ResetMainBG     #下画面の背景を戻す
ResetMainBG     #下画面の背景を戻す
```

### `SetBackGroundTranslate`

```
SetBackGroundTranslate 0 7000 0 700
SetBackGroundTranslate 0 7000 0 700
SetBackGroundTranslate 0 7000 0 700
```

### `setPathLoop`

```
setPathLoop true
setPathLoop true
setPathLoop true
```

### `AnswerC`

```
AnswerC	# 分岐１
AnswerC	# 分岐３
AnswerC	# 分岐３
```

### `CreateShieldCapsule`

```
CreateShieldCapsule <34 106>
CreateShieldCapsule <80 80>
CreateShieldCapsule <128 42>
```

### `RegistCorrectCommand`

```
RegistCorrectCommand 1 CDA
RegistCorrectCommand 2 BD
```

### `CreateRoundCapsule`

```
CreateRoundCapsule <40 35>
CreateRoundCapsule <40 35>
```

### `SetWaitTime`

```
SetWaitTime 1.0
SetWaitTime 10.0
```

### `ScenarioIfNot`

```
ScenarioIfNot first_play
ScenarioIfNot first_play
```

### `ScenarioEndIf`

```
ScenarioEndIf
ScenarioEndIf
```

### `CreateArwingRefresh`

```
CreateArwingRefresh
CreateArwingRefresh
```

### `LoadAnimSequence`

```
LoadAnimSequence rom:/3D/Starfox/Flag/FlagBlue_Flow.anim Flow FlagBlue
LoadAnimSequence rom:/3D/Starfox/Flag/FlagRed_Flow.anim Flow FlagRed
```

### `oh`

```
oh by#this file is setup of effect
```

### `Version`

```
Version mantabuilder 20060926 20:46
```

### `CreateSection`

```
CreateSection 0 -1 0 0 0 0
```

### `BeginSection`

```
BeginSection 0
```

### `RegistAnim`

```
RegistAnim A_walker_Walk rom:/Test_A_Walker_Walk.nsbca
```

### `PushAnim`

```
PushAnim A_Walker Walk A_walker_Walk
```

### `CreateBounds`

```
CreateBounds A_Walker A_Walker
```

### `SetBackGroundProjFlag`

```
SetBackGroundProjFlag false
```

### `SetBackGroundScale`

```
SetBackGroundScale 1.0 1.0 1.0
```

### `SetBackGroundZHack`

```
SetBackGroundZHack 0.5
```

### `SetBackgroundColor`

```
SetBackgroundColor 0 0 0
```

### `StartSetup`

```
StartSetup DemoObject0
```

### `LoadMesh`

```
LoadMesh /3D/Enemy/ShellWalker/ShellWalker.mdl
```

### `LoadAnim`

```
LoadAnim /3D/Enemy/ShellWalker/Walk.anim
```

### `EndSetup`

```
EndSetup
```

### `CreateCamera`

```
CreateCamera 64 0.0
```

### `SetCoordinate`

```
SetCoordinate CAMERA 0 LOCAL PLAYER 0 0.0
```

### `LookAtThing`

```
LookAtThing CAMERA 0 PLAYER 0 <0.0 0.0 0.0> 1.0
```

### `LabelStageSelectNext`

```
LabelStageSelectNext
```

### `AddWeakPoint`

```
AddWeakPoint <0.0 1.0 0.0> <0.6 0.6 0.6>
```

### `AnswerD`

```
AnswerD	# 分岐２
```

### `LabelCorneriaNext`

```
LabelCorneriaNext
```

### `f`

```
f
```

## Commandes vues uniquement en commentaire

Vestiges de développement : probablement supportées par le moteur d'origine mais inutilisées dans la version finale.

- `AddWaitTime` (×1)
- `AddWeakPointVolume` (×26)
- `Amanda` (×1)
- `Aquas` (×14)
- `Arwing` (×89)
- `Ash` (×8)
- `Asteroid` (×11)
- `AsteroidBelt` (×3)
- `Back` (×20)
- `Bill` (×1)
- `Boss` (×12)
- `Button` (×2)
- `Cancel` (×4)
- `Cat` (×1)
- `City` (×3)
- `Clouds` (×1)
- `Code` (×2)
- `Confirm` (×2)
- `Corneria` (×11)
- `CreateBackGround` (×2)
- `CreateEnemy2` (×1)
- `CreateMissileEnemy` (×2)
- `CreatePlayer` (×1)
- `CreateShadow` (×1)
- `Dummy` (×19)
- `Dune` (×1)
- `Each` (×7)
- `End` (×2)
- `Enemy` (×296)
- `EnemyEgg` (×10)
- `Exchange` (×1)
- `Fake` (×1)
- `Falco` (×8)
- `Fichina` (×1)
- `Fog` (×72)
- `Fox` (×8)
- `Friend` (×10)
- `Gallery` (×1)
- `Grass` (×2)
- `Ground` (×175)
- `Hack` (×1)
- `HighDune` (×1)
- `James` (×1)
- `Jodie` (×1)
- `Katarina` (×7)
- `Krystal` (×8)
- `Labe` (×1)
- `Leon` (×8)
- `Life` (×95)
- `Load` (×2)
- `LoadScript` (×1)
- `Lock` (×43)
- `LookAtPos` (×1)
- `LookInDirection` (×1)
- `Lucy` (×1)
- `Marker` (×11)
- `Menu` (×2)
- `Missile` (×1)
- `Mission` (×58)
- `Mountain` (×3)
- `MoveAlong` (×1)
- `MoveToPos` (×2)
- `MoveToThing` (×3)
- `Multi` (×8)
- `Network` (×8)
- `Next` (×2)
- `Oasis` (×1)
- `Octman` (×8)
- `OrbitPos` (×2)
- `Panther` (×8)
- `Peppy` (×1)
- `Planet` (×2)
- `Plasma` (×43)
- `Pulse` (×7)
- `Recording2_D` (×1)
- `Recording_D` (×1)
- `Regist` (×323)
- `RegistFlashModelSet` (×1)
- `RegistStageBase` (×4)
- `Retry` (×1)
- `RockBarrel` (×1)
- `RockGull` (×2)
- `Ruin` (×1)
- `Rule` (×32)
- `Sample` (×131)
- `Sea` (×3)
- `Set` (×2)
- `SetBackGroundPos` (×2)
- `SetBackGroundRotateX` (×1)
- `SetBackGroundRotateZ` (×1)
- `SetBackGroundYHack` (×2)
- `SetRot` (×1)
- `SetScale` (×2)
- `SetSpeed` (×1)
- `Shared` (×7)
- `Shrimp` (×2)
- `Single` (×1)
- `Slippy` (×8)
- `Snow` (×1)
- `Solar` (×8)
- `Space` (×3)
- `Stage` (×143)
- `Stealth` (×43)
- `Sticky` (×1)
- `Target` (×21)
- `Texture` (×9)
- `Titania` (×9)
- `Training` (×2)
- `Universal` (×59)
- `UnlockScenario` (×4)
- `Venom` (×9)
- `Volcanobase` (×1)
- `Weapon` (×14)
- `WiFi` (×1)
- `Wifi` (×1)
- `Wolf` (×8)
- `Zone` (×13)

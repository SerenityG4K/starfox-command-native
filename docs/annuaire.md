# Annuaire du binaire

Ce que le jeu dit de lui-même, par ordre de force de preuve. Rien ici n'est
deviné : chaque ligne cite la chaîne ou la table d'où elle vient.

- **Nom certain** : une table d'enregistrement associe ce nom à cette adresse.
- **Mention unique** : la fonction désigne un seul identifiant. C'est presque
  toujours celle qui crée l'objet de ce nom — presque, pas toujours.
- **Mentions multiples** : une fabrique, un registre ou un aiguillage ; le
  faisceau dit de quoi la fonction s'occupe, pas comment elle s'appelle.

Produit par `tools/symboles.py` ; 622 fonctions touchées.

## arm9

Fichiers source nommés par ses assertions : `script.cpp`.

### Noms certains

| adresse | nom |
|---|---|
| `0x02017408` | `SetPulseRamp` |
| `0x020175C0` | `SetPulseSinWave` |
| `0x02017754` | `PushPulseData` |
| `0x0201783C` | `CreatePulse` |
| `0x0201793C` | `SetMarkerArg` |
| `0x02017978` | `CreateMarker` |
| `0x02017DBC` | `BreakPoint` |
| `0x02017DC0` | `RemoteItemEnd` |
| `0x02017E04` | `RemoteItemBegin` |
| `0x02017E4C` | `Version` |
| `0x02018208` | `LoadAnimSequence` |
| `0x02018390` | `SetConfigParam` |
| `0x02018428` | `SetTexAnimLoop` |
| `0x02018464` | `SetTexAnimSize` |
| `0x02018628` | `PushIndexAnimSeq` |
| `0x02018840` | `PushTexAnimSeq` |
| `0x0201899C` | `PushTexAnim` |
| `0x02018AD8` | `LoadTexAnimPatt` |
| `0x02018B8C` | `CreateTexAnimSeq` |
| `0x02018DE8` | `CreateTexAnimBase` |
| `0x02018F30` | `LoadTexture` |
| `0x020190DC` | `SetBackgroundEffect` |
| `0x020191C0` | `InitBackgroundEffect` |
| `0x020191D0` | `SetStageMode` |
| `0x0201921C` | `SetBackGroundTranslate` |
| `0x020193EC` | `SetBackGroundRotateZ` |
| `0x02019498` | `SetBackGroundRotateX` |
| `0x02019544` | `SetBackGroundRotate` |
| `0x020195F0` | `PushBackGround` |
| `0x02019694` | `SetBackGroundColor` |
| `0x020197DC` | `ClearBackGround` |
| `0x020197EC` | `CreateBackGround` |
| `0x020197F0` | `RegistInformerInfinite` |
| `0x02019800` | `SetHeightInformer` |
| `0x02019838` | `RegistHeightGridObject` |
| `0x02019AA0` | `RegistHeightModel` |
| `0x02019AF8` | `LoadHeightMap` |
| `0x02019B40` | `SetHeightGridOffset` |
| `0x02019C14` | `SetHeightGridSize` |
| `0x02019C8C` | `ClearHeightManager` |
| `0x02019CB0` | `Include` |
| `0x02019CD0` | `VolumeFlags` |
| `0x02019D1C` | `SubVolumeMM` |
| `0x02019FE8` | `SubVolume` |
| `0x0201A27C` | `RegistVolume` |
| `0x0201A6AC` | `IncludeEnemy` |
| `0x0201A6CC` | `SetEnemyState` |
| `0x0201AC48` | `CreateEnemy` |
| `0x0201B470` | `CreateEnemyTemplate` |
| `0x0201B79C` | `EnableEnemyTemplate` |
| `0x0201BF18` | `CreateLODModel` |
| `0x0201C558` | `CreateFlashModel` |
| `0x0201C5F0` | `RegistModelFromSet` |
| `0x0201C8F0` | `RegistFlashModelSet` |
| `0x0201C968` | `RegistModelSet` |
| `0x0201C9E0` | `RegistBillboard` |
| `0x0201CA60` | `RegistModel` |
| `0x0201CAE0` | `AddPathPos` |
| `0x0201CC58` | `CreatePath` |
| `0x0201CF3C` | `ConfigOpen` |
| `0x0201CFB8` | `Pause` |
| `0x0201D030` | `SetTimeOutMode` |
| `0x0201D074` | `SetTimeOutTime` |
| `0x0201D0F0` | `CreatePlayerFromInfo` |
| `0x0201D140` | `ClearPlayerInfo` |
| `0x0201D14C` | `CreatePlayer` |
| `0x0201D4BC` | `CreatePlayerDirect` |
| `0x0201D734` | `EraseEnemyStore` |
| `0x0201D738` | `LoadStageAllClear` |
| `0x0201D748` | `RegistNonTarget` |
| `0x0201D868` | `LoadEnemy` |
| `0x0201D910` | `RegistEnemy` |
| `0x0201DA30` | `RegistEnemyAllClear` |
| `0x0201DAD8` | `SetStageLayer` |
| `0x0201DB58` | `RegistStageLayer` |
| `0x0201DD30` | `LoadStageDirect` |
| `0x0201DD34` | `LoadStage` |
| `0x0201DD78` | `RegistStage` |
| `0x0201DE9C` | `RegistStageAllClear` |
| `0x020ABBF8` | `N3abi17__class_type_infoE` |
| `0x020ABC18` | `N3abi20__si_class_type_infoE` |

### Mention unique

| adresse | mention |
|---|---|
| `0x02063E10` | `_scroll` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x0201DF48` | `overlay_engine`, `overlay_main`, `overlay_wifi_config`, `overlay_callbacks`, `overlay_strategy`, `overlay_menu`, `overlay_callbacks2`, `overlay_wifi2`, `overlay_download`, `overlay_corneria_enemy` … (+15) |
| `0x02038544` | `Information`, `Info_Battles`, `Battle`, `Info_Memory`, `Info_EnemyMemory`, `EnemyMemory`, `Info_LayerMemory`, `LayerMemory`, `Info_StageMemory`, `StageMemory` … (+43) |
| `0x0203966C` | `overlay_strategy`, `overlay_strategy2`, `overlay_menu`, `overlay_menu2`, `overlay_callbacks2`, `overlay_callbacks`, `StageConfig` |
| `0x020408B8` | `BattleSubScene`, `ResultSubScene`, `ContinueSubScene`, `BattleConsoleSubScene`, `BaseState` |
| `0x02040DA0` | `plane_event`, `StatusSubScene` |
| `0x02042594` | `Strategy`, `Strategy_Dump`, `Dump`, `Strategy_Fog`, `Strategy_Fog_Grow`, `Grow`, `Test`, `Strategy_ReturnTitle`, `Strategy_MissionRule` |
| `0x02046E58` | `DemoTargetBand`, `DemoTargetStripe`, `DemoTargetStr`, `DemoTargetCount`, `Star`, `Stage_DemoCamAngle` |
| `0x02048F24` | `ContinueSubScene`, `overlay_callbacks`, `overlay_callbacks2`, `RadarSubScene`, `BattleConsoleSubScene`, `NetworkBattleConsoleScene`, `ResultSubScene` |
| `0x0204955C` | `Stage_DemoCamAngle`, `MainState` |
| `0x02049E98` | `Info_Target`, `Info_Difficulty` |
| `0x0204A21C` | `Info_StageMemory`, `Info_Stage`, `Info_StageLayer` |
| `0x0204B564` | `BattleSubScene`, `ResultSubScene`, `ContinueSubScene`, `BattleConsoleSubScene`, `BaseState`, `overlay_strategy`, `overlay_strategy2` |
| `0x0204C3C8` | `BtnObj`, `PauseObjButton` |
| `0x02053650` | `MultiplayCfg`, `BattleTimer`, `RandomStageSelect`, `ShowGuideCube`, `LockOnDistance`, `LockOnTimeMili`, `BattleTimeLenSec`, `HomingSpeed`, `StealthTimeSec` |
| `0x02053C18` | `MultiplayCfg`, `BattleTimer`, `RandomStageSelect`, `ShowGuideCube`, `LockOnDistance`, `LockOnTimeMili`, `BattleTimeLenSec`, `HomingSpeed`, `StealthTimeSec` |
| `0x0205568C` | `BombSpawned`, `TwinLaserSpawned`, `PlasmaSpawned`, `StealthSpawned`, `BasakaSpawned`, `ChargeSpawned`, `SupplySpawned`, `RemainTime30`, `Default`, `BombCaptured_` … (+12) |
| `0x0205AEA4` | `SmallDamage`, `LargeDamage` |
| `0x02065294` | `Mesh`, `MeshVtxAnimData`, `LODEngine`, `RenderObject`, `Texture`, `Palette`, `Camera`, `ModelParser`, `BaseMeshParser`, `TextureParser` … (+16) |
| `0x0207BA34` | `Component2D`, `ComponentObject`, `ComponentGroup`, `ComponentButton`, `Texture2D`, `Image`, `SclImage`, `PaletteBuffer`, `ImageFrame`, `PlaneData` … (+4) |
| `0x0209E1E4` | `battle_royal`, `rival_battle`, `friend_battle` |
| `0x020A3AF4` | `CreatePath`, `AddPathPos`, `RegistModel`, `RegistBillboard`, `RegistModelSet`, `RegistFlashModelSet`, `RegistModelFromSet`, `CreateFlashModel`, `CreateLODModel`, `ActorObject` … (+45) |

## ovl0

### Noms certains

| adresse | nom |
|---|---|
| `0x021CE60C` | `ContrailScaleX` |
| `0x021CE626` | `ContrailScaleZ` |
| `0x021CE644` | `ContrailResistance` |
| `0x021CE662` | `ContrailFactorX` |

### Mention unique

| adresse | mention |
|---|---|
| `0x02182D60` | `Stars` |
| `0x0218EEF0` | `Snow` |
| `0x02195978` | `GuidBeacon` |
| `0x02196CD0` | `EscapeEggLaunch` |
| `0x021B9C5C` | `MediumPulse` |
| `0x021C654C` | `ExParticle` |
| `0x021C8E10` | `Manta_2_0710a` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x0216D344` | `TrainingLevel_4`, `NearBlast`, `overlay_callbacks2` |
| `0x021751C0` | `ActorGroup`, `Actor`, `ActorCallback`, `CollidableObject`, `ProximityQuery`, `ColObjectRef`, `CollisionCell`, `ColQuadNode`, `ColVolume`, `ScriptParser` … (+76) |
| `0x02181380` | `Enemys`, `Enemy_Shot`, `EnemyShot`, `Enemy_Shot_KillTime`, `Enemy_Missile`, `Missile`, `Enemy_Missile_LifeSpan`, `Enemy_LaserTurret`, `LaserTurret`, `Enemy_LaserTurret_ScoutRange` … (+28) |
| `0x02188F1C` | `_lod`, `Arwing`, `ArwingWeakPoint` |
| `0x0218B2E4` | `General`, `General_Fog`, `General_Fog_Enable`, `Enable`, `General_Fog_Mode`, `Mode`, `COLOR_ALPHA`, `ALPHA`, `FogMode`, `General_Fog_Slope` … (+16) |
| `0x0218C838` | `Slope`, `FogTable`, `Offset`, `Color_R`, `Color_G`, `Color_B`, `Alpha`, `General_Fog_FogTable`, `Curve`, `_General_Fog` … (+7) |
| `0x0219180C` | `ContinueArwing`, `ContinueNums` |
| `0x021934B8` | `LastStar`, `Default`, `StarBar`, `StarNums`, `StarLargeNum`, `StarAlphaNum`, `Star` |
| `0x0219C018` | `ArwingContrail`, `RollingR`, `RollingL` |
| `0x0219CD6C` | `ArwingContrail`, `ArwingConfig`, `Arwing_Scale`, `Contrail`, `ContrailScaleX`, `ContrailScaleZ`, `ContrailResistance`, `ContrailFactorX`, `ContrailFactorY`, `ContrailFactorZ` |
| `0x021ACF9C` | `TargetNums`, `SightLock` |
| `0x021B8A28` | `Text`, `NumW`, `AlertNum` |
| `0x021BC24C` | `RegistPlayerState`, `SetModelName`, `SetBoostOnName`, `SetBoostOffName`, `SetBoostEffectName`, `SetWingEffectOffset`, `SetSingleLaserOffset`, `SetTwinLaserOffset` |
| `0x021BD5D8` | `fill_screen_main`, `fill_screen_sub` |

## ovl2

### Mention unique

| adresse | mention |
|---|---|
| `0x021DFA30` | `LaserReflect` |
| `0x021E1690` | `ShotActorCallback` |
| `0x021E1B84` | `UpdateThread` |
| `0x021E2D54` | `SmartBomb` |
| `0x021E4820` | `WinningCameraCallback` |
| `0x021ECD8C` | `SmartBomb` |
| `0x021EF584` | `StageBorder` |
| `0x021F0DD0` | `ShotReflect` |
| `0x021F67B8` | `StageBorder` |
| `0x021FD1F0` | `MedalItem` |
| `0x02200C24` | `PauseSubScene` |
| `0x02205030` | `PauseSubScene` |
| `0x022083F8` | `Result` |
| `0x0220A020` | `MessageFace` |
| `0x0220F6A8` | `Locked` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x021E1558` | `ShotActor`, `ShotActorCallback` |
| `0x021E1BB4` | `PlayerStorageParser`, `ArwingRollingVehicle`, `ArwingVehicle`, `ArwingDynamics`, `ArwingDynamicsModule`, `ArwingCockpitModule`, `ArwingCockpitRelativeCtrl`, `ArwingCockpitRelRailsCtrl`, `BattleMessageParser`, `EffectStorageData` … (+1) |
| `0x021E6CEC` | `ChargeShot`, `SingleLaser` |
| `0x021E9618` | `ArwingConfig`, `Control`, `DPad`, `Buttons`, `Down`, `Left`, `Right`, `RotLeft`, `RotRight`, `Boost` |
| `0x021F071C` | `SmallDamage`, `LargeDamage` |
| `0x021F3594` | `MedalItem`, `Explosion` |
| `0x021F4230` | `Dummy`, `Explosion2`, `Explosion` |
| `0x021F5038` | `ArwingBrake`, `ArwingBoost` |
| `0x021F78C4` | `NetworkArwingDynamicsPlayer`, `NetworkArwingDynamicsEnemy`, `ArwingDynamicsPlayerFoxSingle`, `ArwingDynamicsPlayerFoxTwin`, `ArwingDynamicsPlayerFoxSinglePlasma`, `ArwingDynamicsPlayerFoxOther`, `ArwingDynamicsPlayer`, `ArwingRollingVehicle` |
| `0x021F8480` | `ArwingConfig`, `Max_Height`, `Min_Height`, `Arwing_GroundHeightOffset`, `idle_height`, `shot_type`, `Relative` |
| `0x021F8F94` | `ArwingConfig`, `Max_Height`, `Min_Height`, `Arwing_GroundHeightOffset`, `idle_height`, `shot_type`, `Config_MedalDirect`, `MedalDirect`, `HeightOffset`, `reverse_control` |
| `0x021FB3F8` | `ArwingMsg`, `StageBorder`, `MPItemCollectMsg`, `ItemCollectMsg`, `GuidBeacon`, `ItemGet`, `EnemyDestroy`, `EnemyWarning`, `SetControl`, `LaserReflect` … (+1) |
| `0x021FEB8C` | `DefaultGroup`, `Zapper`, `CollideVehicle`, `Arwing_Scale`, `SingleLaser`, `Muzzle` |
| `0x021FF338` | `Arwing_Scale`, `Muzzle`, `Plasma` |
| `0x021FF840` | `Arwing_Scale`, `SingleLaser` |
| `0x021FFE94` | `Arwing_Scale`, `SingleLaser` |
| `0x022023B8` | `_drag_bomb`, `TrainingLevel_4` |
| `0x022029D8` | `radar_back`, `radar_front`, `_cockpit`, `_cockpit2`, `_buttons`, `_lights` |
| `0x0220371C` | `radar_back`, `radar_front`, `_cockpit`, `_cockpit2`, `_buttons`, `_lights`, `_radar`, `_counter` |
| `0x02205AC4` | `radar_back`, `radar_front`, `radar_button`, `BombIcon`, `BombArea`, `RadarBackPlane` |
| `0x02209318` | `MediumPulse`, `LightPulse` |
| `0x02211540` | `PLock`, `Warning` |
| `0x02212080` | `ShieldG_S`, `ShieldW_S`, `ShieldBorder_S`, `ShieldG_M`, `ShieldW_M`, `ShieldBorder_M`, `ShieldG_L`, `ShieldW_L`, `ShieldBorder_L`, `Boost_S` … (+12) |
| `0x02213B9C` | `ArwingConfig`, `Sights`, `Analog_control`, `Scroll_stage`, `Sight_delay`, `Fast_sight_speed`, `Fast_sights`, `Shoot_sights`, `ChargeShot`, `LockSightRange` … (+1) |
| `0x022148B4` | `ArwingConfig`, `Sights`, `Analog_control`, `Scroll_stage`, `Sight_delay`, `Fast_sight_speed`, `Fast_sights`, `Shoot_sights` |
| `0x02217EB8` | `Ygreen`, `display_`, `name_`, `display_num` |
| `0x0221976C` | `RegistEffect`, `CreateTexAnimBase`, `CreateTexAnimSeq`, `LoadTexAnimPatt`, `PushTexAnim`, `PushTexAnimSeq`, `SetTexAnimSize`, `SetTexAnimLoop` |

## ovl3

### Noms certains

| adresse | nom |
|---|---|
| `0x022157AC` | `Strategy_Fog_Color_G` |
| `0x022157C2` | `Strategy_Fog_Color_B` |
| `0x022157DA` | `Strategy_Fog_Color_A` |
| `0x02215CE0` | `Strategy_Player_Arwing_Fuel` |
| `0x02215D02` | `Strategy_Player_Arwing_Speed` |
| `0x02215D2A` | `Strategy_Player_Arwing_BattleTime` |

### Mention unique

| adresse | mention |
|---|---|
| `0x021DF858` | `FogErase` |
| `0x021E48F8` | `EventBattleZone` |
| `0x021E4E58` | `SmallDamage` |
| `0x021FC77C` | `ArwingVehicle` |
| `0x022054F4` | `TimeTemp` |
| `0x02212568` | `Information` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x021E0A78` | `Strategy`, `Strategy_Fog`, `Strategy_Fog_Color`, `Color`, `Strategy_Fog_Color_R`, `Strategy_Fog_Color_G`, `Strategy_Fog_Color_B`, `Strategy_Fog_Color_A`, `Alpha` |
| `0x021E109C` | `FogMapSubScene`, `TransitionScreenSubScene`, `MessageWindowSubScene` |
| `0x021E2260` | `IconTown_Player`, `IconTown_Enemy`, `IconTown_Unknown`, `IconTown_Free`, `IconTown_Boss_Barrier`, `IconTown_Boss_Unknown`, `IconTown_Boss_Open`, `IconCitySearchRange`, `IconCityArert` |
| `0x021E369C` | `IconCitySearchRange`, `IconCityArert` |
| `0x021EA110` | `CapsuleTouch`, `GFMissile`, `Info_Target_Star`, `AsteroidRoute`, `RouteDraw` |
| `0x021EC6F4` | `ArwingMsgBattleResult`, `ArwingRouteChangeMode` |
| `0x021ECB10` | `EventBattleZone`, `EventArwingSOS`, `GfMissileSmall`, `ArwingIcons`, `ArwingShadows`, `GreatFoxIcons`, `GreatFoxShadows`, `ArwingRouteSnap`, `GfMissile`, `GfMissileTarget` … (+1) |
| `0x021ED7FC` | `Strategy`, `Strategy_Player`, `Player`, `Strategy_Player_Arwing`, `Arwing`, `Strategy_Player_Arwing_Fuel`, `Fuel`, `Strategy_Player_Arwing_Speed`, `Speed`, `Strategy_Player_Arwing_BattleTime` … (+3) |
| `0x021EDCAC` | `ArwingSet`, `GreatFoxMissileObject`, `ArwingRouteSubScene` |
| `0x021EF59C` | `ArwingIcons`, `ArwingShadows`, `GreatFoxIcons`, `GreatFoxShadows`, `Arwing`, `GuideGroup` |
| `0x021F2764` | `IconEnemy_A`, `IconEnemyShadow_A`, `IconEnemyDetection`, `IconEnemyArrow`, `IconEnemy_Missile`, `IconEnemyShadow_Missile`, `IconEnemyLockon`, `IconEnemy_MissileA`, `IconEnemy_Size1`, `IconEnemy_Size` … (+1) |
| `0x021F2E60` | `Strategy`, `Strategy_Enemy`, `Enemy`, `Strategy_Enemy_Zako`, `Zako`, `Strategy_Enemy_Zako_MoveLength`, `MoveLength`, `Strategy_Enemy_Zako_Speed`, `Speed`, `Strategy_Enemy_Missile` … (+2) |
| `0x021F4B98` | `IconEnemyDetection`, `IconEnemyArrow`, `EnemyIcon` |
| `0x021F855C` | `MapObjectCursor`, `ItemGetText`, `ItemLifeBase`, `ItemLifeBar`, `ItemGetNum`, `ItemPerfect` |
| `0x021F905C` | `MapMoveObject`, `MapItemObject`, `MapLineObject`, `MapLineObjectShort`, `MapObjectSubScene` |
| `0x021F98B8` | `ItemLifeBase`, `ItemLifeBar`, `ItemGetNum`, `ItemPerfect`, `MediumPulse`, `LightPulse` |
| `0x021FAA6C` | `ItemGetText`, `ItemGetNum` |
| `0x021FD194` | `SimuStage`, `Corneria`, `SeaBase`, `Grass`, `GrassBase`, `CityA`, `CityB`, `CityBase`, `Mountain`, `MountainBase` … (+3) |
| `0x021FDF70` | `Space`, `Cosmos`, `Weltraum`, `Spazio` |
| `0x02202718` | `StrategyBasic`, `MissionComplete` |
| `0x02203EDC` | `plane_base`, `plane_event`, `StatusSubScene`, `ToDoListSubScene`, `main_front`, `TransitionScreenSubScene`, `MessageWindowSubScene`, `BattleSubScene`, `BaseState`, `StrategyScene` |
| `0x022062E4` | `StatusSubScene`, `ToDoListSubScene` |
| `0x022066D0` | `plane_event`, `CapsuleTouch` |
| `0x02207D30` | `plane_base`, `plane_event`, `main_front`, `TransitionScreenSubScene`, `MessageWindowSubScene`, `FogMapSubScene`, `BackScreenSubScene`, `EnemyManageSubScene`, `DetailInfoSubScene`, `ArwingRouteSubScene` … (+1) |
| `0x02208804` | `GoBtj`, `RoundObjButton` |
| `0x0220B728` | `Missile`, `Enemy`, `AnyCity` |
| `0x0220CAD0` | `city_destroy`, `catch_arwing`, `no_greatfox_stage`, `boss_extra` |
| `0x0220D01C` | `Missile`, `Enemy`, `AnyCity` |
| `0x0220DE00` | `boss_extra`, `Enemy`, `model_name`, `enemy_name`, `escape_enemy`, `encount_pos`, `enemy_num`, `missile_silo` |
| `0x0220EF5C` | `ClearRule`, `GetAllBase`, `DestroyAllEnemy`, `DestroyTargetEnemy`, `HeavyZone`, `GreatFox` |
| `0x02210088` | `RegistRound`, `CreateEncountEnemy`, `RegistStage`, `ZoneInfo`, `MissileZoneInfo`, `RegistMessage`, `TopLeft`, `TopRight`, `Left`, `Right` … (+24) |
| `0x022119FC` | `StatusSubScene`, `ToDoListSubScene`, `BattleSubScene`, `BaseState` |
| `0x02212E7C` | `StatusSubScene`, `ToDoListSubScene`, `main_front`, `plane_event`, `BackScreenSubScene` |
| `0x02213E4C` | `EnemyTemplateParser`, `EnemyCountParser`, `EnemyInfo` |

## ovl4

### Mention unique

| adresse | mention |
|---|---|
| `0x021E3234` | `MenuMessage` |
| `0x021E852C` | `Title` |
| `0x021EC1E8` | `StandardButton` |
| `0x021EC6DC` | `StandardButton` |
| `0x021EFEE0` | `KeyGet` |
| `0x021FEFF4` | `name_tex_` |
| `0x02210A2C` | `f_list_date_tex` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x021E02BC` | `TitleBarText`, `MenuBase` |
| `0x021E06AC` | `WiFiMenuScene`, `MPMenuScene`, `RootMenuSubScene`, `TitleSubScene` |
| `0x021E5160` | `ok_button`, `cancel_button`, `next_button`, `back_button` |
| `0x021E6E48` | `Test`, `Test_TestBattle`, `TestBattle`, `Test_TestBattle_Go`, `Test_TestBattle_Arwing`, `Arwing`, `RANDOM`, `Krystal`, `Falco`, `Slippy` … (+13) |
| `0x021E7630` | `Test_TestBattle`, `TestBattleLevel`, `Test_TestBattle_Difficulty`, `Difficulty`, `TestBattleDifficulty`, `Test_TestBattle_Spawn`, `SpawnNum` |
| `0x021E94C0` | `Title`, `ESRB`, `Logo`, `TitleSubScene` |
| `0x021EB4DC` | `DebugLayoutScriptStart`, `DebugLayoutScriptEnd`, `StartSinglePlayGame`, `StartStageSelect`, `ExecuteStageSelect`, `StartAdvatise`, `OptionSetting`, `BackMainMenu`, `StartRecordingScene`, `EndRecordingScene` … (+4) |
| `0x021F1E08` | `Normal`, `Select`, `Object`, `FontArea` |
| `0x021F2724` | `None`, `Custom2`, `Standard`, `Custom`, `no_shadow` |
| `0x021F3F28` | `Standard`, `Custom` |
| `0x021F4830` | `Alpha`, `Depth`, `PolygonID`, `Visible` |
| `0x021F6548` | `PLTT4`, `PLTT16`, `PLTT256`, `COMP4x4`, `A5I3`, `DIRECT` |
| `0x021F6B00` | `Release`, `RegistTexture`, `RegistTextArea`, `CreateGroup`, `CreateBoard`, `CreateObject`, `CreateNumber`, `UpdateStatus` |
| `0x021F710C` | `Release`, `RegistTexture`, `RegistTextArea`, `CreateGroup`, `CreateBoard`, `CreateObject`, `CreateNumber`, `UpdateStatus` |
| `0x021F7A48` | `WiFiError`, `board_group`, `error_code` |
| `0x021F81F0` | `rank_`, `_group`, `name_tex_`, `num_star_`, `num_rate_` |
| `0x021F8D38` | `num_star_`, `my_rank_`, `num_win_`, `num_drop_`, `num_total_` |
| `0x021FA044` | `WiFiScore`, `num_star`, `num_win`, `num_drop`, `num_total`, `num_rate` |
| `0x021FA5E8` | `overlay_wifi2`, `overlay_wifi`, `overlay_menu2`, `WiFiBattleBaseScene` |
| `0x021FAAA0` | `board_group`, `WiFiMenuDown`, `play_button`, `friend_button`, `setting_button`, `RootMenuSubScene`, `Connect1`, `info_board`, `Connect2`, `Disconnect1` … (+29) |
| `0x021FFA84` | `overlay_download`, `overlay_wifi`, `overlay_menu2` |
| `0x0220007C` | `info_board`, `RootMenuSubScene`, `MPMainUp`, `MPMainDown`, `seek_pilot_button`, `MPEntryUp`, `MPEntryDown`, `board_group`, `message_group`, `DownloadWait` |
| `0x02207F7C` | `QuestionBoard`, `msg_board`, `wave_board` |
| `0x022083FC` | `msg_board`, `wave_board`, `text_group`, `VoiceRecoard`, `MicSetting`, `low_button`, `middle_button`, `high_button`, `Question`, `VoiceCheck` … (+5) |
| `0x0220A094` | `_inv`, `key_en_none` |
| `0x0220A7BC` | `NickName`, `mode_button`, `mode_button0`, `mode_button1`, `mode_button2`, `text_area_tex` |
| `0x0220BEA0` | `WiFiFriendCode`, `list_button`, `regist_button`, `confirm_button`, `exchange_button`, `FriendListU`, `FriendListD`, `msg_board`, `RegistCodeU`, `RegistCodeD` … (+45) |
| `0x0220EDF4` | `FC_button_`, `name_tex_` |
| `0x0220FAE8` | `num_now_page`, `num_total_page` |
| `0x0221034C` | `name_tex_`, `f_list_date_tex` |
| `0x02211058` | `flower_panther`, `Next`, `Prev`, `Cancel`, `Back`, `Delete`, `Retry`, `CloseEntry` |

## ovl5

### Mention unique

| adresse | mention |
|---|---|
| `0x02229754` | `EnemyMothership` |
| `0x0222AB88` | `Default` |
| `0x02233548` | `Default` |
| `0x02235B98` | `LaserR` |
| `0x02247450` | `Move` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x02228618` | `ProngA`, `Main` |
| `0x02228BB0` | `ProngB`, `ProngC`, `ProngD`, `ProngA`, `Move`, `Message`, `Spin`, `Pulse`, `SearchBeam` |
| `0x02229ED8` | `GuidBeacon`, `EscapeEggLaunch`, `Move`, `Message`, `EnemyRestore`, `Explode`, `Falldown`, `RollingExplode` |
| `0x0222DD50` | `Shoot`, `Patrol`, `LineUp`, `Swoop`, `Charge`, `Follow` |
| `0x0222FD84` | `Shoot`, `Orbit` |
| `0x02234540` | `Chase`, `Patrol`, `Default` |
| `0x02238640` | `BasicFlyVehicle`, `WeaponEnemy`, `RingLaser`, `Shoot`, `Default` |
| `0x0223BD04` | `Default`, `Splash` |
| `0x0223F128` | `Move`, `Return`, `Free`, `mSemirossiaHand`, `SemirossiaHand`, `Hand` |
| `0x02241AE0` | `Relative`, `Float`, `Hakofugu` |
| `0x02242FB8` | `Default`, `NausHint`, `MoveAbout`, `OandS`, `LookAt`, `RunAway`, `HeightAdjust` |
| `0x022467EC` | `Relative`, `Default` |

## ovl6

### Mention unique

| adresse | mention |
|---|---|
| `0x022279B0` | `BeamLaser` |
| `0x02228F28` | `EnemyMothership` |
| `0x02235404` | `Default` |
| `0x0223AAA0` | `EnemyBio` |
| `0x02243BE0` | `Default` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x02227D5C` | `ProngA`, `Main` |
| `0x02228364` | `ProngB`, `ProngC`, `ProngD`, `ProngA`, `Move`, `Message`, `Spin`, `Pulse`, `SearchBeam`, `MeshRenderer` … (+22) |
| `0x022296AC` | `EscapeEggLaunch`, `GuidBeacon`, `Move`, `Message`, `EnemyRestore`, `Explode`, `Falldown`, `RollingExplode` |
| `0x0222A0A0` | `Disp`, `Move2`, `SquareShield`, `TriangleShield`, `BasicFlyVehicle`, `EnemyShieldBit`, `ShieldEnemy` |
| `0x0222B3AC` | `Move`, `Shoot` |
| `0x0222FD70` | `Shoot`, `Follow`, `Patrol`, `Swoop`, `LineUp`, `Charge` |
| `0x02239674` | `AmoebaAttack`, `Glob` |
| `0x02239D08` | `InFront`, `Patrol`, `Shoot`, `Swipe`, `Free`, `Cross`, `Line` |
| `0x0223FC40` | `InFront`, `Patrol`, `Shoot`, `Free`, `Spin`, `Square` |
| `0x02243694` | `SimpleVehicle`, `WeaponEnemy`, `SXwhite` |
| `0x0224BD04` | `RedCube`, `Default` |

## ovl7

### Mention unique

| adresse | mention |
|---|---|
| `0x022368D4` | `Default` |
| `0x02237AF4` | `Default` |
| `0x0223EFE4` | `EnemyMothership` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x02226E8C` | `Monitor`, `Exit`, `Enter`, `Relative`, `Setup` |
| `0x022284FC` | `MorasBlob`, `MorasTail`, `MeshRenderer` |
| `0x02228C0C` | `MorasBlob`, `Charge`, `Orbit`, `Detect` |
| `0x0222B9B8` | `Detect`, `NonBossDamage1`, `NonBossDamage2`, `NonBossDamage3`, `ShamanDown`, `OctoDamage0`, `OctoDamage1`, `DevilSharkWeak`, `Open`, `Normal` … (+16) |
| `0x0222D2E4` | `ecDevilShark`, `ecDevilSharkParts`, `ecDevilSharkWeak`, `ecKobanZame` |
| `0x0222D7C4` | `ThreadVariable`, `DetectThread`, `EnemyDevilShark`, `DevilSharkWeak`, `Remora`, `ecDevilShark`, `ecDevilSharkParts`, `ecDevilSharkWeak`, `ecKobanZame`, `DevilSharkParts` … (+7) |
| `0x02231C68` | `Patrol`, `InFront`, `Free`, `Spin`, `Shoot`, `Square`, `Attacker3` |
| `0x022330C4` | `Charge`, `Patrol`, `Shoot`, `Swoop`, `LineUp`, `Follow` |
| `0x02238D24` | `RedCube`, `NausHelpOpen` |
| `0x0223DF14` | `ProngA`, `Main` |
| `0x0223E490` | `ProngB`, `ProngC`, `ProngD`, `ProngA`, `Move`, `Message`, `Spin`, `Pulse`, `SearchBeam` |
| `0x0223F768` | `GuidBeacon`, `EscapeEggLaunch`, `Move`, `Message`, `EnemyRestore`, `Explode`, `Falldown`, `RollingExplode` |
| `0x02241984` | `EnemyGokai`, `WeaponFireBomb` |
| `0x022428D8` | `Move`, `Spin`, `Charge`, `Live` |
| `0x022448E8` | `Attack`, `Shoot`, `LionFinA`, `LionFinB`, `Orbit`, `Detect`, `Bubble` |
| `0x0224D004` | `DarumaBase`, `DarumaMiddle`, `DarumaTop`, `Daruma` |
| `0x022506B8` | `Damage`, `EchizenWPA`, `EchizenBarA`, `Cannon`, `Laser`, `Enemy`, `Wait` |
| `0x02252C38` | `CloseHatch`, `NausHelpStart`, `Diamond`, `OpenHatch` |

## ovl8

### Mention unique

| adresse | mention |
|---|---|
| `0x02230C30` | `Shoot` |
| `0x02231364` | `Default` |
| `0x02233584` | `Loop` |
| `0x0223B9B8` | `EnemyMothership` |
| `0x0223FE44` | `Default` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x02229F44` | `Disp`, `Move2`, `SquareShield`, `TriangleShield`, `BasicFlyVehicle`, `EnemyShieldBit`, `ShieldEnemy` |
| `0x02230B9C` | `CollideVehicle`, `WeaponEnemy`, `RingLaser`, `Default` |
| `0x022327F8` | `SHLaser`, `RingLaser` |
| `0x0223AD2C` | `ProngA`, `Main`, `SHOT_SPEED` |
| `0x0223AE64` | `ProngB`, `ProngC`, `ProngD`, `ProngA`, `Move`, `Message`, `Spin`, `Pulse`, `SearchBeam` |
| `0x0223C13C` | `GuidBeacon`, `EscapeEggLaunch`, `Move`, `Message`, `EnemyRestore`, `Explode`, `Falldown`, `RollingExplode` |
| `0x0223D024` | `WartWart`, `Defence`, `Patrol` |
| `0x0223F8A0` | `WeaponEnemy`, `SimpleVehicle`, `SXwhite` |
| `0x0224182C` | `Shoot`, `Spin`, `Patrol`, `InFront`, `Free`, `Square` |
| `0x022432A8` | `Shoot`, `Patrol`, `Swoop`, `LineUp`, `Charge`, `Follow` |
| `0x02247C7C` | `Move`, `Shoot`, `EnemyKBeeSting`, `KBeeSting`, `Scale` |
| `0x02249C2C` | `Shoot`, `SXwhite`, `SXyellow`, `Default`, `EnemyKBeeSting` |
| `0x0224A1E4` | `Shoot`, `Default` |
| `0x02252A6C` | `BasicFlyVehicle`, `WeaponEnemy`, `RingLaser`, `Shoot`, `Default` |

## ovl9

### Noms certains

| adresse | nom |
|---|---|
| `0x02256AF8` | `Slippy` |
| `0x02256B08` | `MeshRenderer` |
| `0x02256B20` | `ZAZAN_2` |
| `0x02256D84` | `ZazanChatMsg` |
| `0x02256D9C` | `LeonChatMsg` |
| `0x02256DB0` | `PantherChatMsg` |
| `0x02256DC8` | `WolfChatMsg` |

### Mention unique

| adresse | mention |
|---|---|
| `0x02227170` | `RingLaser` |
| `0x0222A39C` | `SLIPPY` |
| `0x02239848` | `SolarSatellite` |
| `0x0223B93C` | `EnemyWolfen` |
| `0x02241E10` | `UtsuboSimple` |
| `0x02244914` | `EnemyMothership` |
| `0x0224A134` | `RandomShot` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x02229418` | `WakeUp`, `SLIPPY`, `QuartMsg`, `HalfMsg`, `ThreeQuartMsg`, `OCTMAN`, `ZAZAN_2`, `ZazanDeathMsg`, `ZazanHitMsg`, `ZazanChatMsg` … (+15) |
| `0x0222AB24` | `SLIPPY`, `ZAZAN`, `LeonHitMsg`, `LEON`, `PantherHitMsg`, `PANTHER`, `WolfHitMsg`, `WOLF`, `Slippy`, `WakeUp` … (+1) |
| `0x0222EB64` | `Default`, `NonBossDamage1`, `NonBossDamage2`, `NonBossDamage3`, `ShamanDown`, `OctoDamage0`, `OctoDamage1`, `DevilSharkWeak`, `Open`, `Normal` … (+16) |
| `0x02230560` | `ecDevilShark`, `ecDevilSharkParts`, `ecDevilSharkWeak`, `ecKobanZame` |
| `0x02230A40` | `BubbleGenerator`, `EnemyDevilShark`, `DevilSharkWeak`, `Remora`, `ecDevilShark`, `ecDevilSharkParts`, `ecDevilSharkWeak`, `ecKobanZame`, `DevilSharkParts`, `KobanZame` … (+7) |
| `0x02232534` | `Appear`, `OctoPodPod`, `OctoShield`, `Hide`, `Setup`, `Messages`, `Intro`, `Wriggle`, `eSlippy`, `OctoPod` |
| `0x02234D70` | `Default`, `Intro`, `MDHFire`, `MDHBob`, `MDHDrop`, `HitByAsh`, `NausTip1`, `Dead`, `Taunt` |
| `0x02235FBC` | `MDHBob`, `MDHDrop`, `NausTip1`, `Taunt` |
| `0x022363DC` | `Patrol`, `Backup`, `Anim`, `Stand` |
| `0x0223828C` | `Taunt`, `Drone`, `Missile`, `NausTip2`, `Burning` |
| `0x02238774` | `Patrol`, `Drone`, `Missile`, `Turn`, `Return`, `Beam`, `Magma`, `M_Launcher`, `D_Launcher` |
| `0x02239D3C` | `ThreadVariable`, `PatrolThread`, `SolarSatellite`, `TurnThread`, `ReturnThread`, `Launcher`, `SatMissileAction`, `SatFighter`, `AstBeam`, `BeamThread` … (+3) |
| `0x0223AA60` | `Patrol`, `eAstMine` |
| `0x0223ADF0` | `InFront`, `Behind`, `Bomb`, `Locked`, `Strafe`, `Loop` |
| `0x0223DA4C` | `MagmaRockA`, `MagmaRockB`, `MagmaRockC`, `MagmaRockD` |
| `0x0223EFF4` | `UtsuPart`, `BreakUp`, `Clock`, `Whip`, `UtsuBodyDam` |
| `0x0223FB30` | `Clock`, `UtsuHeadDam`, `Slot`, `Hint2`, `Hint1` |
| `0x0223FFD0` | `CHEER`, `UtsuboSimpleHide` |
| `0x02240640` | `Default`, `Attack`, `Hide`, `Dead`, `Turn`, `Beam`, `BreakUp`, `Clock`, `Whip`, `Hint2` … (+11) |
| `0x02242618` | `ThreadVariable`, `HideThread`, `TurnThread`, `UtsuboSimple`, `UtsuPart`, `UtsuSpine`, `MoveThread`, `BoundsThread`, `HiddenThread`, `EmergeThread` … (+3) |
| `0x02243844` | `ProngA`, `Main` |
| `0x02243DC0` | `ProngB`, `ProngC`, `ProngD`, `ProngA`, `Move`, `Message`, `Spin`, `Pulse`, `SearchBeam` |
| `0x02245098` | `GuidBeacon`, `EscapeEggLaunch`, `Move`, `Message`, `EnemyRestore`, `Explode`, `Falldown`, `RollingExplode` |
| `0x0224C284` | `SLIPPY`, `Behind`, `WolfLaser` |
| `0x0224E3B8` | `Missile`, `SatMissile`, `Default` |
| `0x0224FBE8` | `MDMissile`, `Fireball`, `Default` |
| `0x02250C54` | `OctDamage1`, `HitPot`, `MissPot`, `Octo20sec`, `Octo30sec`, `NausHintReflect1`, `NausHintReflect`, `NausHintHide`, `Default` |
| `0x02251DB4` | `RedCube`, `NausHelpOpen` |

## ovl10

### Mention unique

| adresse | mention |
|---|---|
| `0x0222F830` | `DebateContent` |
| `0x02239514` | `ScenarioPartMainWindow` |
| `0x02239E74` | `ScenarioPartMainWindow` |
| `0x0223F6D4` | `MissionTitie` |
| `0x02240768` | `Page` |
| `0x02240B24` | `StoryBackImage` |
| `0x022428F8` | `Arwing` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x02226098` | `MissionClearScene`, `MissionFailedScene`, `SelectM`, `button_1`, `button_2`, `button_3`, `bg_group` |
| `0x022275A8` | `RootMenu`, `button_single`, `button_multi`, `button_wifi`, `button_option` |
| `0x02228D04` | `EndingGallerySubScene`, `PilotGallerySubScene`, `SinglePlay`, `button_start`, `button_con`, `button_stage`, `button_gallery`, `button_tuto`, `StrategyChallengeScene`, `RootMenuSubScene` … (+7) |
| `0x0222AFC2` | `info_board`, `AllClear`, `Control`, `btn_normal` |
| `0x0222C64C` | `arwing_1`, `arwing_2`, `arwing_3`, `arwing_4`, `down_1`, `down_2`, `down_3`, `down_4`, `missile_1`, `missile_2` … (+3) |
| `0x0222CA24` | `capsule_L_3`, `capsule_L_4`, `capsule_L_5`, `capsule_S_1`, `capsule_S_2`, `capsule_S_3`, `capsule_S_4`, `capsule_S_5`, `num_continue`, `icon_continue` … (+4) |
| `0x0222D478` | `capsule_group`, `MissionClear`, `button_save`, `arwing_group`, `missile_group`, `hit_point_group`, `total_point_group` |
| `0x0222E2F8` | `MissionFailed`, `button_retry`, `button_exit` |
| `0x02233364` | `MenuMessageSubScene`, `ScenarioPartMainWindowSubScene`, `ScenarioPartBranchWindowSubScene` |
| `0x02233870` | `DebateContent`, `ScenarioMainBG`, `ScenarioPart` |
| `0x02234AE4` | `StrategyChallengeScene`, `StrategyScene`, `GameStorySubScene`, `StaffrollSubScene`, `UnlockInformationSubScene`, `MissionTitleScene` |
| `0x022356FC` | `Test`, `ScenarioLock`, `Test_ScenarioLock_Blue`, `Test_ScenarioLock_Bronze`, `Test_ScenarioLock_Silver` |
| `0x0223A400` | `MemberIconTextureLeft`, `MemberIconTextureRight` |
| `0x0223E9B4` | `MemberTalks`, `FinishScenarioPart`, `NextMission`, `GreatFoxMovesTo`, `SetBranchLevel`, `SetBranchMember`, `SetBranchPlanet`, `BranchScenario`, `UnlockScenario`, `ScenarioLevelUp` … (+6) |
| `0x022410A8` | `StoryBackImage`, `GameStory`, `button_prev`, `button_next`, `button_start`, `button_end`, `button_skip`, `button_back`, `button_cont`, `button_group` … (+1) |
| `0x022437E0` | `Arwing`, `Pilot`, `button_1`, `button_2`, `button_3`, `button_4`, `button_prev`, `button_next`, `board_chara`, `board_arwing` … (+1) |
| `0x0224483C` | `ending_list2_txt`, `ending_list1_txt` |
| `0x022453F4` | `Gallery`, `button_1`, `button_2`, `button_3` |
| `0x02245FF0` | `EndingGallerySubScene`, `UnlockInformationSubScene` |
| `0x022478E0` | `StaffRoll`, `FontSet`, `Font_2`, `STARFOX`, `COMMAND`, `CREDITS`, `Producer`, `Director`, `Network`, `Direction` … (+10) |
| `0x0224A2F0` | `StaffrollSubScene`, `StaffData` |

## ovl11

### Mention unique

| adresse | mention |
|---|---|
| `0x0222A090` | `ecPigma` |
| `0x0222E1B0` | `GoriRunner` |
| `0x0222F714` | `Shoot` |
| `0x0222FFBC` | `Crash` |
| `0x0223B824` | `RingLaser` |
| `0x0223EC94` | `Hint2` |
| `0x022410A4` | `Utsubo` |
| `0x02243C48` | `EnemyBio` |
| `0x022456C0` | `EnemyMothership` |
| `0x0224B454` | `RandomShot` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x02228F48` | `Pigma`, `Face1`, `Face2`, `Face3`, `Mangled`, `PigmaA`, `PigmaB`, `PigmaC`, `PigmaD`, `Circle` … (+3) |
| `0x0222A5D8` | `EnemyPigma`, `Messages`, `ecPigma`, `PigmaBox`, `PigmaShot`, `PigmaShotPart`, `PigmaFlash`, `ThreadVariable`, `CircleThread`, `Setupthread` … (+4) |
| `0x0222BD00` | `Spark`, `BasicFlyVehicle`, `Spark_1` |
| `0x0222D4C4` | `Dead`, `Face`, `Taunt`, `NausTip1`, `Patrol`, `Move`, `Turn`, `Bounds`, `Shoot`, `Drop` … (+2) |
| `0x022301D8` | `Move`, `Shoot`, `Default`, `EnemyKBeeSting`, `Crash`, `NausHelp`, `KBeeSting`, `Scale`, `Intro`, `Regular0` … (+1) |
| `0x02232720` | `Move`, `Shoot`, `WeakCrawl`, `WeakPoint`, `WormCrawl` |
| `0x02233CBC` | `SimpleVehicle`, `SandwormWeapon`, `Rock`, `Default` |
| `0x02234150` | `Regular1`, `Regular0`, `Shoot`, `Sand`, `Default` |
| `0x022355BC` | `Shoot`, `SXwhite`, `SXyellow`, `Default`, `EnemyKBeeSting` |
| `0x02235CCC` | `Shoot`, `Default` |
| `0x02238138` | `GoriRockBig`, `Beat`, `Face` |
| `0x0223DB24` | `UtsuSPPart`, `UtsuPart` |
| `0x0223E108` | `UtsuPart`, `BreakUp`, `Clock`, `Whip`, `UtsuBodyDam` |
| `0x0223EFA0` | `Clock`, `Hint1`, `UtsuHeadDam`, `Slot` |
| `0x0223F178` | `Dead`, `BreakUp`, `Clock`, `Whip`, `Hint2`, `Hint1`, `CHEER`, `UtsuboHide`, `Move`, `Turn` … (+12) |
| `0x022418AC` | `Utsubo`, `UtsuPart`, `ThreadVariable`, `UtsuSpine`, `MoveThread`, `TurnThread`, `BoundsThread`, `HideThread`, `HiddenThread`, `EmergeThread` … (+3) |
| `0x02242820` | `AmoebaAttack`, `Glob` |
| `0x02242EB4` | `InFront`, `Patrol`, `Shoot`, `Swipe`, `Free`, `Cross`, `Line` |
| `0x022445F0` | `ProngA`, `Main` |
| `0x02244B6C` | `ProngB`, `ProngC`, `ProngD`, `ProngA`, `Move`, `Message`, `Spin`, `Pulse`, `SearchBeam` |
| `0x02245E44` | `GuidBeacon`, `EscapeEggLaunch`, `Move`, `Message`, `EnemyRestore`, `Explode`, `Falldown`, `RollingExplode` |
| `0x02248A60` | `RedCube`, `Default` |

## ovl12

### Mention unique

| adresse | mention |
|---|---|
| `0x02225FB4` | `ArwingMsg` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x02227844` | `Shoot`, `Patrol`, `Swoop`, `LineUp`, `Charge`, `Follow` |
| `0x02229C84` | `BasicFlyVehicle`, `WeaponEnemy`, `RingLaser`, `Shoot`, `Default` |

## ovl13

### Mention unique

| adresse | mention |
|---|---|
| `0x0222F93C` | `ecAnglor` |
| `0x02236380` | `CrabThigh` |
| `0x02238EDC` | `ecDeathCrab` |
| `0x0223D9EC` | `FireBeam` |
| `0x0223E3A4` | `RedCube` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x02227EAC` | `ShieldE`, `ShieldD`, `ShieldC`, `ShieldB`, `ShieldA` |
| `0x02228D54` | `AChargeG`, `AChargeF`, `AChargeE`, `AChargeD`, `AChargeC`, `AChargeB`, `AChargeA` |
| `0x022295E0` | `AnglorTaunt2`, `AnglorTaunt1`, `AnglorShotA`, `AnglorShotB`, `AnglorShotC`, `AnglorShotD`, `ShieldDown2`, `ShieldDown1` |
| `0x0222ACFC` | `AChargeG`, `AChargeF`, `AChargeE`, `AChargeD`, `AChargeC`, `AChargeB`, `AChargeA` |
| `0x0222B46C` | `AChargeA`, `AnglorShotA` |
| `0x0222D4C0` | `Mouth`, `SecondFace`, `Flutter`, `TouchA`, `TouchB`, `WeakPoint` |
| `0x0222E024` | `ShieldDown`, `CHEER`, `BozoidA`, `ShieldA`, `Flying`, `Search`, `Laser`, `SuperCharge`, `Missile`, `Suck` … (+9) |
| `0x022303A0` | `ShieldDown`, `EnemyAnglor`, `WeakPoint`, `Cyclone`, `Search`, `Laser`, `SuperCharge`, `LastThroes`, `Waiting`, `Rotty` … (+18) |
| `0x022334BC` | `OpenClose`, `Shoot`, `Wait`, `ClawFire` |
| `0x0223727C` | `Flying`, `Fall`, `Reset`, `Stand`, `TakeOff` |
| `0x0223763C` | `WeakPoint`, `Flying`, `Monitor`, `CrabMouth`, `Fall`, `Reset`, `Stand`, `TakeOff`, `UpdateLegs`, `Shuffle` … (+11) |
| `0x02239690` | `ThreadVariable`, `EnemyDeathCrab`, `TakeOff`, `ecDeathCrab`, `DeathCrabEye`, `DeathCrabStalk`, `DeathCrabLeg`, `DeathCrabThigh`, `BubbleShotAction`, `ClawThread` … (+4) |
| `0x0223A5EC` | `Default`, `Gatlin`, `Taunt`, `Move`, `Slink`, `Vulnerable`, `Beam`, `SplitEnemy` |
| `0x0223E934` | `Attack`, `DestroyAllParts` |
| `0x02240730` | `Bubble`, `FireBubble`, `WeakPoint`, `MouthOpen` |
| `0x02241110` | `Bubble`, `Heaven` |
| `0x022415F4` | `ClawClose`, `Wait`, `Shoot` |
| `0x022422BC` | `OffLight`, `Monitor`, `Search` |
| `0x02243EF8` | `Chomp`, `Suck`, `Missile` |
| `0x022446BC` | `AnglorBasic1`, `AnglorSearch2`, `AnglorSearch1`, `AnglorSuck2`, `AnglorSuck1`, `ShieldDown2`, `ShieldDown1`, `AnglorTaunt2`, `AnglorTaunt1`, `NausHelpSuck` … (+5) |
| `0x022451AC` | `Chomp`, `Suck`, `NausHelpSuck` |

## ovl14

### Noms certains

| adresse | nom |
|---|---|
| `0x022459F4` | `Flatland` |
| `0x022459FC` | `Mountain` |
| `0x02245A0C` | `Fortress` |

### Mention unique

| adresse | mention |
|---|---|
| `0x02229420` | `Arwing` |
| `0x0222B7FC` | `DetailHeader` |
| `0x02231588` | `StrategyBasic` |
| `0x02235634` | `_icon` |
| `0x02236EA0` | `_down` |
| `0x02237494` | `Status` |
| `0x0223A59C` | `Todo` |
| `0x0223C338` | `TodoMakeTodo` |
| `0x0223D7C0` | `_Demo` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x02228E88` | `TargetEnemy`, `_Demo` |
| `0x0222BCB4` | `DetailHeader`, `DetailBoard`, `_twin`, `_plasma`, `_multi` |
| `0x0222D82C` | `DetailHeader`, `DetailBoard`, `DetailInfo` |
| `0x0222E368` | `eWolf`, `eSandWorm`, `eZazan`, `eGorirunner`, `eKillerBee`, `eOctopod`, `eDevilShark`, `eDeathCrab`, `ePigmaBorg`, `eMonarchDodora` … (+42) |
| `0x02233750` | `IconFuelSet`, `IconTimeSet`, `IconLifeSet`, `IconMissileSet`, `IconEnemyCore`, `IconTalkPoint`, `Explode16`, `RingBlue`, `RingYellow`, `RingGreen` … (+3) |
| `0x02234D38` | `Explode`, `RingEffect`, `ArwingShieldInfo` |
| `0x022362FC` | `RingBlue`, `RingGreen`, `RingGreenLarge` |
| `0x02238588` | `OpenWindow`, `ActiveWindow`, `Status` |
| `0x0223AE88` | `TaskTouch`, `SelectChara` |
| `0x0223E2A4` | `boss_deathcrab`, `boss_zazanga9`, `boss_splitter`, `boss_biobrain`, `boss_arrowhead`, `boss_starwolf`, `boss_devilshark`, `boss_octopod`, `boss_solar`, `boss_jogorilla` … (+5) |
| `0x0223F2A4` | `Co_town`, `training_lv1`, `training_lv2`, `training_lv3`, `training_lv4`, `Co_base`, `Co_missile`, `Co_sea`, `Grass`, `Co_grass` … (+86) |
| `0x02242F80` | `EncSnd`, `TextExtra`, `MoveCursor`, `DrawText` |
| `0x02243C18` | `MessageWindow`, `MesWindowButton` |

## ovl15

### Mention unique

| adresse | mention |
|---|---|
| `0x0222B65C` | `NetworkBattle` |
| `0x0222BC30` | `NBEx` |
| `0x0222C380` | `NBResult` |
| `0x0222D5A8` | `BattleSubScene` |
| `0x0222DBC8` | `BattleSceneContol` |
| `0x0222E83C` | `BattleSubScene` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x022282E0` | `ArwingConfig`, `Max_Height` |
| `0x0222A1F4` | `TwinLaser_Item`, `WeaponChangeItem`, `Plasma_Item`, `Lock_Item`, `SmartBomb_Item`, `Stealth_Item` |
| `0x02234C28` | `HotSpotResponse`, `HTML` |

## ovl16

### Mention unique

| adresse | mention |
|---|---|
| `0x0222D114` | `Bunshin` |
| `0x02232E30` | `DropMine` |
| `0x022333DC` | `EnemyDeLalou` |
| `0x0224377C` | `EnemyMothership` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x0222937C` | `Patrol`, `Move`, `Turn`, `Bounds`, `FCannonTop` |
| `0x0222B264` | `Move`, `Restart`, `FireBum` |
| `0x0222C1D4` | `Attacker2`, `JumpToAr` |
| `0x0222E814` | `EnemyKingDodoraBody`, `EnemyKingDodora`, `KingDodoTail` |
| `0x0222F2A0` | `KingDodoTail`, `KingDodoBody` |
| `0x0222FB88` | `KingDodoBody`, `Basic`, `Charge`, `Ideal`, `Fire` |
| `0x02232538` | `FireSplash`, `DropMineFlash` |
| `0x02234E6C` | `Default`, `MDHFire`, `MDHBob`, `MDHDrop`, `HitByAsh`, `Intro`, `NausTip1`, `Dead`, `Taunt` |
| `0x02235C60` | `MonDoHead`, `MDHFire`, `MDHBob`, `MDHDrop`, `NausTip1`, `Taunt`, `BuryHead` |
| `0x022364A4` | `Patrol`, `Backup`, `Anim`, `Stand` |
| `0x02239298` | `MDMissile`, `Fireball`, `Default` |
| `0x0223B370` | `Shoot`, `Default` |
| `0x0223C5F0` | `FireBum`, `Default` |
| `0x022426A4` | `ProngA`, `Main` |
| `0x02242C28` | `ProngB`, `ProngC`, `ProngD`, `ProngA`, `Move`, `Message`, `Spin`, `Pulse`, `SearchBeam` |
| `0x02243F00` | `GuidBeacon`, `EscapeEggLaunch`, `Move`, `Message`, `EnemyRestore`, `Explode`, `Falldown`, `RollingExplode` |
| `0x022456B8` | `Move`, `Defend`, `Attack`, `SarulegL`, `SarulegR` |
| `0x02246300` | `Shoot`, `Move`, `Patrol`, `Turn`, `Bounds` |
| `0x02247508` | `Balloon`, `BalloonHit` |
| `0x02248E20` | `Shoot`, `Patrol`, `Loop`, `Flash`, `Anim`, `Sand`, `eMultiMissile` |
| `0x02249E6C` | `Move`, `Spin`, `Wait`, `Launch`, `Chase`, `Crash` |
| `0x0224F780` | `MultiMissile`, `Default` |
| `0x02253B40` | `BasicFlyVehicle`, `WeaponEnemy`, `RingLaser`, `Shoot`, `Default` |

## ovl17

### Mention unique

| adresse | mention |
|---|---|
| `0x022322A8` | `Default` |
| `0x0223EA64` | `EnemyMothership` |
| `0x02244FF4` | `Default` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x02228768` | `MorasBlob`, `MorasTail`, `MeshRenderer` |
| `0x02228E78` | `MorasBlob`, `Charge`, `Orbit`, `Detect` |
| `0x0222A730` | `Setup`, `Monitor`, `Exit`, `Enter`, `Relative` |
| `0x0222B784` | `Patrol`, `Loop` |
| `0x0222BC4C` | `Patrol`, `Shoot`, `Loop` |
| `0x0222E6AC` | `Move`, `Restart`, `FireBum` |
| `0x0222FAD4` | `Defence`, `NausHint` |
| `0x0222FB04` | `WartWart`, `Patrol`, `Defence` |
| `0x02233E60` | `Shoot`, `Default` |
| `0x0223C280` | `MagmaRockA`, `MagmaRockB`, `MagmaRockC`, `MagmaRockD` |
| `0x0223D98C` | `ProngA`, `Main` |
| `0x0223DF10` | `ProngB`, `ProngC`, `ProngD`, `ProngA`, `Move`, `Message`, `Spin`, `Pulse`, `SearchBeam` |
| `0x0223F1E8` | `GuidBeacon`, `EscapeEggLaunch`, `Move`, `Message`, `EnemyRestore`, `Explode`, `Falldown`, `RollingExplode` |
| `0x022400A8` | `Move`, `Defend`, `Attack`, `SarulegL`, `SarulegR` |
| `0x02247D6C` | `BasicFlyVehicle`, `WeaponEnemy`, `RingLaser`, `Shoot`, `Default` |

## ovl18

### Mention unique

| adresse | mention |
|---|---|
| `0x0222D758` | `NausHint` |
| `0x02239424` | `Default` |
| `0x02241C20` | `EnemyMothership` |
| `0x02248258` | `MijinkoArm` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x02228290` | `SHORT`, `SHOT_SPEED` |
| `0x02229E14` | `Shoot`, `Attack`, `Float`, `PorcupineFace`, `PorcupineSpike` |
| `0x0222E210` | `Wait`, `Launch`, `Charge` |
| `0x02234110` | `Move`, `Spin`, `Wait`, `Launch`, `Chase`, `Crash` |
| `0x02238A98` | `GaroaShot`, `Shoot` |
| `0x02239FEC` | `ElecBeam`, `AstMine`, `Bomb` |
| `0x0223B3E0` | `MultiMissile`, `Default` |
| `0x0223C444` | `BasicFlyVehicle`, `WeaponEnemy`, `RingLaser`, `Shoot`, `Default` |
| `0x0223E7B0` | `DevilShark2Body`, `DevilShark2Tail` |
| `0x02240B50` | `ProngA`, `Main` |
| `0x022410CC` | `ProngB`, `ProngC`, `ProngD`, `ProngA`, `Move`, `Message`, `Spin`, `Pulse`, `SearchBeam` |
| `0x022423A4` | `GuidBeacon`, `EscapeEggLaunch`, `Move`, `Message`, `EnemyRestore`, `Explode`, `Falldown`, `RollingExplode` |
| `0x02242A7C` | `CancerLegs`, `CancerClaw`, `Waddle`, `ClawUp`, `Strafe` |
| `0x02243E48` | `Move`, `Spin`, `Charge`, `Live` |
| `0x02244BD4` | `NausHint`, `MoveAbout`, `OandS`, `LookAt`, `RunAway`, `HeightAdjust`, `Default` |
| `0x02246CC8` | `Attack`, `Detect`, `LionFinA`, `LionFinB`, `Orbit`, `Shoot`, `Bubble` |
| `0x0224931C` | `MijinkoHand`, `Wave` |

## ovl19

### Mention unique

| adresse | mention |
|---|---|
| `0x0222F170` | `LaserR` |
| `0x02231438` | `AttAtt` |
| `0x02236374` | `Default` |
| `0x022371F8` | `Default` |
| `0x0223D998` | `EnemyMothership` |
| `0x022406BC` | `EnemyEchizenWP` |
| `0x02240E20` | `NausHint` |
| `0x02243C68` | `Bunshin` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x0222D514` | `Move`, `Restart`, `FireBum` |
| `0x0222E100` | `Patrol`, `Loop` |
| `0x0222E5C8` | `Patrol`, `Shoot`, `Loop` |
| `0x0223023C` | `Chase`, `Move`, `Spin`, `Wait`, `Launch`, `Crash` |
| `0x0223182C` | `Patrol`, `Shoot`, `LineUp`, `Swoop`, `Charge`, `Follow` |
| `0x022358DC` | `Shoot`, `Default` |
| `0x02238680` | `Chase`, `Patrol`, `Default` |
| `0x0223C8C8` | `ProngA`, `Main` |
| `0x0223CE44` | `ProngB`, `ProngC`, `ProngD`, `ProngA`, `Move`, `Message`, `Spin`, `Pulse`, `SearchBeam` |
| `0x0223E11C` | `GuidBeacon`, `EscapeEggLaunch`, `Move`, `Message`, `EnemyRestore`, `Explode`, `Falldown`, `RollingExplode` |
| `0x0223E62C` | `JShrimpUp`, `LaserR` |
| `0x02240E58` | `Damage`, `Cannon`, `EchizenWPB`, `EchizenBarB`, `Laser`, `EchizenWPC`, `EchizenBarC`, `Fighter`, `EchizenWPA`, `EchizenBarA` … (+1) |
| `0x02242EF8` | `DodoraTail`, `DodoraBody` |
| `0x0224585C` | `Shoot`, `Default` |
| `0x02246A94` | `Diamond`, `OpenHatch`, `Default` |
| `0x02248FD8` | `BasicFlyVehicle`, `WeaponEnemy`, `RingLaser`, `Shoot`, `Default` |

## ovl20

### Mention unique

| adresse | mention |
|---|---|
| `0x02233AA8` | `Bunshin` |
| `0x022380E0` | `LaserR` |
| `0x02240D7C` | `EnemyMothership` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x0222F240` | `Default`, `SmartBomb` |
| `0x02232584` | `Splash`, `Default` |
| `0x02234D04` | `Shoot`, `Default`, `NausHint`, `FaffAbout`, `ToArwing`, `Escape` |
| `0x02238108` | `Shoot`, `Default` |
| `0x02239CDC` | `BarnacleShot`, `Default` |
| `0x0223FC48` | `ProngA`, `Main` |
| `0x022401E0` | `ProngB`, `ProngC`, `ProngD`, `ProngA`, `Move`, `Message`, `Spin`, `Pulse`, `SearchBeam` |
| `0x02241500` | `GuidBeacon`, `EscapeEggLaunch`, `Move`, `Message`, `EnemyRestore`, `Explode`, `Falldown`, `RollingExplode` |
| `0x02242C14` | `Move`, `Restart`, `FireBum` |
| `0x02243A8C` | `FireBum`, `Default` |
| `0x022460D8` | `BasicFlyVehicle`, `WeaponEnemy`, `RingLaser`, `Shoot`, `Default` |

## ovl23

### Noms certains

| adresse | nom |
|---|---|
| `0x0226E54E` | `overlay_katarina_enemy` |
| `0x0226E56A` | `overlay_aquas_enemy` |
| `0x0226E584` | `overlay_solar_enemy` |
| `0x0226E5A0` | `overlay_macbeth_enemy` |
| `0x0226E5BE` | `overlay_titania_enemy` |
| `0x0226E5DA` | `overlay_area6_enemy` |
| `0x0226E5F4` | `overlay_venom_enemy` |
| `0x0226E626` | `overlay_boss2_enemy` |
| `0x0226E640` | `overlay_extra_enemy` |
| `0x0226E65C` | `overlay_missile_enemy` |

### Mention unique

| adresse | mention |
|---|---|
| `0x02268DA8` | `GuidBeacon` |
| `0x0226D240` | `EscapeAnglar` |
| `0x0226D96C` | `Default` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x02258A60` | `EnemyStorageData`, `MarkerObject`, `ModelSwitch`, `AnimEvent`, `BubbleExplosionGen` |
| `0x0225A7E0` | `CorneriaEnemyOverlay`, `AsteroidEnemyOverlay`, `FichinaEnemyOverlay`, `KatarinaEnemyOverlay`, `AquasEnemyOverlay`, `SolarEnemyOverlay`, `MacbethEnemyOverlay`, `TitaniaEnemyOverlay`, `Area6EnemyOverlay`, `VenomEnemyOverlay` … (+34) |
| `0x0225F6D8` | `RegistEnemy`, `SetTextureSet`, `SetModel`, `SetBillboard`, `SetFlashModel`, `SetInfiniteView`, `SetVolume`, `SetSubVolume`, `SetAnim`, `SetCallback` … (+14) |
| `0x022660B4` | `Once`, `True`, `TRUE`, `Both`, `TriggerIndex`, `TargetTriggerIndex`, `TriggerFunc`, `TriggerVariable` |
| `0x0226776C` | `SetEnemyState`, `CreateEnemyTemplate` |
| `0x0226C608` | `DefaultGroup`, `AnglorHoleDemo`, `Anglor`, `Arwing` |

## ovl24

Fichiers source nommés par ses assertions : `gpiInfo.c`, `gpiSearch.c`.

## ovl25

Fichiers source nommés par ses assertions : `DownloadManager.cpp`.

### Noms certains

| adresse | nom |
|---|---|
| `0x0226435C` | `loadArchive` |
| `0x02264370` | `_table` |

### Mentions multiples

| adresse | mentions |
|---|---|
| `0x0225A3A8` | `loadArchive`, `_file_image`, `_table` |
| `0x022616B8` | `handi_`, `board_group`, `group_p_`, `name_tex_`, `win_` |
| `0x022623F0` | `board_group`, `info_board`, `DownloadU`, `DownloadD`, `btn_handi_low`, `btn_handi_middle`, `btn_handi_high`, `btn_ctr_nor`, `btn_ctr_rev`, `EntryWait` |


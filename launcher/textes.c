/*
 * Les textes du lanceur : anglais, français, japonais.
 *
 * Le lanceur publié parle anglais par défaut ; le français et le japonais se choisissent en haut à droite de sa
 * fenêtre, et le choix est retenu dans lanceur.ini (« interface = en | fr | ja »). C'est la langue du lanceur
 * seulement : celle du jeu se choisit dans ses Réglages, parmi celles que porte la cartouche.
 *
 * Les noms propres du jeu (Arwing, Wolfen, les pilotes) suivent l'usage de chaque langue. Les chaînes gardent
 * les formats de _snwprintf : %s et %ls une chaîne large, %hs une chaîne étroite, %d un nombre.
 *
 * Sous licence MIT.
 */
#include "textes.h"

int g_interface = INTERFACE_EN;

static const char *const CODES[NB_INTERFACES] = {"en", "fr", "ja"};

const char *code_interface(int i) { return i >= 0 && i < NB_INTERFACES ? CODES[i] : CODES[0]; }

int interface_du_code(const wchar_t *code) {
    for (int i = 0; i < NB_INTERFACES; i++)
        if (code[0] == (wchar_t)CODES[i][0] && code[1] == (wchar_t)CODES[i][1] && !code[2]) return i;
    return -1;
}

static const wchar_t *const TEXTES[NB_TEXTES][NB_INTERFACES] = {
    /* ---- l'accueil ---- */
    [T_TITRE_FENETRE] = {L"Star Fox Command — native port %s", L"Star Fox Command — portage natif %s",
                         L"Star Fox Command — ネイティブ移植版 %s"},
    [T_SOUS_TITRE] = {L"NATIVE PORT  ·  VERSION %s", L"PORTAGE NATIF  ·  VERSION %s", L"ネイティブ移植版  ·  バージョン %s"},
    [T_JOUER] = {L"PLAY", L"JOUER", L"プレイ"},
    [T_PREPARER] = {L"BUILD THE GAME", L"PRÉPARER LE JEU", L"ゲームを準備"},
    [T_PREPARATION] = {L"BUILDING…", L"PRÉPARATION…", L"準備中…"},
    [T_MENU_REGLAGES] = {L"SETTINGS", L"RÉGLAGES", L"設定"},
    [T_MENU_CONTROLES] = {L"CONTROLS", L"CONTRÔLES", L"操作"},
    [T_MENU_MODS] = {L"MODS", L"MODS", L"MOD"},
    [T_MENU_QUITTER] = {L"QUIT", L"QUITTER", L"終了"},
    [T_CARTOUCHE_TITRE] = {L"ROM", L"CARTOUCHE", L"ROM"},
    [T_CHANGER] = {L"change  ›", L"changer  ›", L"変更  ›"},
    [T_CHOISIR] = {L"choose  ›", L"choisir  ›", L"選択  ›"},
    [T_AUCUNE] = {L"None yet: choose your Star Fox Command (Europe) ROM.",
                  L"Aucune : désignez la vôtre — Star Fox Command, version européenne.",
                  L"未選択：お持ちの「スターフォックス コマンド」欧州版を指定してください。"},
    [T_NON_RECONNUE] = {L"This ROM is not recognized.", L"Cette cartouche n'est pas reconnue.",
                        L"このROMは認識できません。"},
    [T_IL_FAUT] = {L"Star Fox Command, European version (ASFP), is required.",
                   L"Il faut Star Fox Command, version européenne (ASFP).",
                   L"「スターフォックス コマンド」欧州版（ASFP）が必要です。"},
    [T_EN_PREPARATION] = {L"Building the game…", L"Préparation du jeu…", L"ゲームを準備しています…"},
    [T_PRET] = {L"✓  The game is ready.", L"✓  Le jeu est prêt.", L"✓  ゲームの準備ができました。"},
    [T_SAUVEGARDE] = {L"Your progress is saved in sauvegarde\\starfox_command.sav",
                      L"La partie s'enregistre dans sauvegarde\\starfox_command.sav",
                      L"セーブデータの保存先：sauvegarde\\starfox_command.sav"},
    [T_PAS_CONSTRUIT] = {L"The game is not built yet.", L"Le jeu n'est pas encore construit.",
                         L"ゲームはまだ準備されていません。"},
    [T_PAS_CONSTRUIT_2] = {L"“Build the game” builds it on your PC, from your ROM.",
                           L"« Préparer le jeu » le construit chez vous, depuis votre cartouche.",
                           L"「ゲームを準備」で、お持ちのROMからこのPC上に構築します。"},
    [T_DEFILE] = {
        L"STAR FOX COMMAND  ·  NATIVE PORT  ·  the whole game recompiled to C, and built on your PC from your own "
        L"ROM  ·  up to 120 frames per second  ·  twice the draw distance  ·  keyboard, gamepad, mods  ·  "
        L"nothing from the game is shipped: these pictures come from your ROM  ·  good luck, pilot!  ·  ",
        L"STAR FOX COMMAND  ·  PORTAGE NATIF  ·  le jeu entier recompilé en C, et construit chez vous depuis votre "
        L"cartouche  ·  jusqu'à 120 images par seconde  ·  le décor deux fois plus loin  ·  clavier, manette, mods  ·  "
        L"rien du jeu n'est livré : ces images viennent de votre cartouche  ·  bon vol, pilote !  ·  ",
        L"STAR FOX COMMAND  ·  ネイティブ移植版  ·  ゲーム全体をCに再コンパイルし、お持ちのROMからあなたのPC上で構築  ·  "
        L"最大120fps  ·  描画距離2倍  ·  キーボード・ゲームパッド・MOD対応  ·  "
        L"ゲームのデータは一切同梱していません。これらの画像はあなたのROMから読み込んでいます  ·  健闘を祈る、パイロット！  ·  "},

    /* ---- Réglages ---- */
    [T_L_CARTOUCHE] = {L"ROM", L"Cartouche", L"ROM"},
    [T_PARCOURIR] = {L"Browse…", L"Parcourir…", L"参照…"},
    [T_L_LANGUE] = {L"Game language", L"Langue du jeu", L"ゲームの言語"},
    [T_NOTE_LANGUE] = {L"(the ones on your ROM: the game has no menu to change it)",
                       L"(celles de votre cartouche : le jeu n'a pas de menu pour la changer)",
                       L"（ROMに収録された言語。ゲーム内に変更メニューはありません）"},
    [T_L_FINESSE] = {L"Resolution", L"Finesse", L"解像度"},
    [T_FINESSE_1] = {L"Like the console", L"Comme la console", L"本体と同じ"},
    [T_FINESSE_2] = {L"Double", L"Double", L"2倍"},
    [T_FINESSE_3] = {L"Triple", L"Triple", L"3倍"},
    [T_FINESSE_4] = {L"Quadruple", L"Quadruple", L"4倍"},
    [T_NOTE_FINESSE] = {L"(3D rendered finer, then scaled down)", L"(la 3D est calculée plus finement, puis réduite)",
                        L"（3Dを高解像度で描画して縮小）"},
    [T_L_TEXTURES] = {L"Textures", L"Textures", L"テクスチャ"},
    [T_TEXTURES_1] = {L"Sharp", L"Nettes", L"くっきり"},
    [T_TEXTURES_2] = {L"Smoothed", L"Adoucies", L"なめらか"},
    [T_TEXTURES_3] = {L"Smoothed at a distance", L"Adoucies au loin", L"遠くのみなめらか"},
    [T_L_CADENCE] = {L"Frames per s.", L"Images par s.", L"フレームレート"},
    [T_NOTE_CADENCE] = {L"(the screen glides between game frames)", L"(l'écran se place entre deux images du jeu)",
                        L"（ゲームの2フレームの間を補間して表示）"},
    [T_L_DISTANCE] = {L"Distance", L"Distance", L"描画距離"},
    [T_DISTANCE_1] = {L"Like the console", L"Comme la console", L"本体と同じ"},
    [T_DISTANCE_2] = {L"Double", L"Double", L"2倍"},
    [T_NOTE_DISTANCE] = {L"(scenery, enemies and shots: twice as far)", L"(décor, ennemis et tirs : deux fois plus loin)",
                         L"（背景・敵・弾が2倍遠くまで）"},
    [T_SON] = {L"Sound", L"Son", L"サウンド"},
    [T_SPECIALISTES] = {L"For experts", L"Pour les spécialistes", L"上級者向け"},
    [T_L_REACTION] = {L"Response", L"Réaction", L"応答"},
    [T_REACTION_1] = {L"Smooth — the screen glides between two game frames (recommended)",
                      L"Fluide — l'écran glisse entre deux images du jeu (recommandé)",
                      L"なめらか — ゲームの2フレーム間を補間（推奨）"},
    [T_REACTION_2] = {L"Immediate — the latest frame, its motion carried forward",
                      L"Immédiate — la plus récente, son mouvement prolongé",
                      L"即時 — 最新のフレームを表示し、動きを先読み"},
    [T_REACTION_3] = {L"Per scene — smooth in menus, immediate in flight",
                      L"Selon la scène — fluide dans les menus, immédiate en vol",
                      L"場面ごと — メニューではなめらか、飛行中は即時"},

    /* ---- Contrôles ---- */
    [T_BOUTONS_CONSOLE] = {L"Console buttons", L"Boutons de la console", L"本体のボタン"},
    [T_CROIX] = {L"D-pad", L"Croix directionnelle", L"十字ボタン"},
    [T_RACCOURCIS] = {L"Port shortcuts", L"Raccourcis du portage", L"移植版のショートカット"},
    [T_HAUT] = {L"Up", L"Haut", L"上"},
    [T_BAS] = {L"Down", L"Bas", L"下"},
    [T_GAUCHE] = {L"Left", L"Gauche", L"左"},
    [T_DROITE] = {L"Right", L"Droite", L"右"},
    [T_RAPIDE] = {L"Fast forward (hold)", L"Avance rapide (tenue)", L"早送り（長押し）"},
    [T_JOURNAL] = {L"Report a display glitch", L"Noter un défaut d'image", L"表示の不具合を記録"},
    [T_NOTE_TOUCHES] = {L"Click a command, then press its new key. The mouse stays the stylus, and the gamepad keeps "
                        L"its controls.",
                        L"Cliquez sur une commande, puis appuyez sur sa nouvelle touche. La souris reste le stylet, "
                        L"et la manette garde ses commandes.",
                        L"コマンドをクリックしてから、新しいキーを押してください。マウスはタッチペンのままで、"
                        L"ゲームパッドの操作も変わりません。"},
    [T_CAPTURE] = {L"Press the new key for “%s”. Esc: cancel.", L"Appuyez sur la nouvelle touche de « %s ». Échap : annuler.",
                   L"「%s」の新しいキーを押してください。Esc：キャンセル"},
    [T_TOUCHE_WINDOWS] = {L"The Windows key would open the Start menu: choose another one.",
                          L"La touche Windows ouvrirait le menu Démarrer : choisissez-en une autre.",
                          L"Windowsキーはスタートメニューを開いてしまいます。別のキーを選んでください。"},
    [T_ECHANGE] = {L"The %s key was used for “%s”: “%s” takes %s instead.",
                   L"La touche %s servait à « %s » : « %s » prend %s à la place.",
                   L"%sキーは「%s」に使われていました。「%s」には代わりに%sを割り当てます。"},
    [T_ORIGINE_REVENUE] = {L"The default keys are back.", L"Les touches d'origine sont revenues.",
                           L"初期設定のキーに戻しました。"},
    [T_ORIGINE] = {L"Default keys", L"Touches d'origine", L"初期設定に戻す"},
    [T_TOUCHE_N] = {L"Key %d", L"Touche %d", L"キー %d"},
    [T_AUCUNE_TOUCHE] = {L"(none)", L"(aucune)", L"（なし）"},
    [T_MANETTE] = {L"Gamepad", L"Manette", L"ゲームパッド"},
    [T_STICK] = {L"Stick, up/down", L"Stick, en hauteur", L"スティック（上下）"},
    [T_AXE_1] = {L"Pushing the stick climbs — like the stylus (recommended)",
                 L"Pousser le stick fait monter — comme le stylet (recommandé)",
                 L"スティックを前に倒すと上昇 — タッチペンと同じ（推奨）"},
    [T_AXE_2] = {L"Pushing the stick dives — like Star Fox 64",
                 L"Pousser le stick fait piquer — comme Star Fox 64",
                 L"スティックを前に倒すと下降 — スターフォックス64と同じ"},

    /* ---- Mods ---- */
    [T_VAISSEAU_DE] = {L"Ship of", L"Le vaisseau de", L"パイロット"},
    [T_M_LASER] = {L"Laser", L"Laser", L"レーザー"},
    [T_M_VERROU] = {L"Lock-on", L"Verrouillage", L"ロックオン"},
    [T_M_JAUGE] = {L"Boost gauge", L"Jauge du turbo", L"ブーストゲージ"},
    [T_M_APPARENCE] = {L"Look", L"Apparence", L"外見"},
    [T_LASER_0] = {L"none", L"aucun", L"なし"},
    [T_LASER_1] = {L"single", L"simple", L"シングル"},
    [T_LASER_2] = {L"twin", L"double", L"ツイン"},
    [T_LASER_3] = {L"plasma", L"plasma", L"プラズマ"},
    [T_LASER_4] = {L"curved shot", L"tir courbe", L"カーブショット"},
    [T_LASER_5] = {L"Zapper", L"Zapper", L"ザッパー"},
    [T_VERROU_0] = {L"none", L"aucun", L"なし"},
    [T_VERROU_1] = {L"single", L"simple", L"シングル"},
    [T_VERROU_2] = {L"multiple", L"multiple", L"マルチ"},
    [T_VERROU_3] = {L"wide", L"large", L"ワイド"},
    [T_JAUGE_0] = {L"small", L"petite", L"小"},
    [T_JAUGE_1] = {L"medium", L"moyenne", L"中"},
    [T_JAUGE_2] = {L"large", L"grande", L"大"},
    [T_CHOIX_LASER_1] = {L"Single", L"Simple", L"シングル"},
    [T_CHOIX_LASER_2] = {L"Twin", L"Double", L"ツイン"},
    [T_CHOIX_LASER_3] = {L"Plasma", L"Plasma", L"プラズマ"},
    [T_CHOIX_LASER_4] = {L"Curved shot (Katt's)", L"Tir courbe (celui de Katt)", L"カーブショット（キャット）"},
    [T_CHOIX_LASER_5] = {L"Zapper (Panther's)", L"Zapper (celui de Panther)", L"ザッパー（パンサー）"},
    [T_CHOIX_VERROU_1] = {L"Single", L"Simple", L"シングル"},
    [T_CHOIX_VERROU_2] = {L"Multiple", L"Multiple", L"マルチ"},
    [T_CHOIX_VERROU_3] = {L"Wide (Leon's)", L"Large (celui de Leon)", L"ワイド（レオン）"},
    [T_CHOIX_JAUGE_1] = {L"Small", L"Petite", L"小"},
    [T_CHOIX_JAUGE_2] = {L"Medium", L"Moyenne", L"中"},
    [T_CHOIX_JAUGE_3] = {L"Large", L"Grande", L"大"},
    [T_VAISSEAU_1] = {L"Fox's Arwing II", L"L'Arwing II de Fox", L"フォックスのアーウィンII"},
    [T_VAISSEAU_2] = {L"Krystal's Arwing", L"L'Arwing de Krystal", L"クリスタルのアーウィン"},
    [T_VAISSEAU_3] = {L"Falco's Arwing", L"L'Arwing de Falco", L"ファルコのアーウィン"},
    [T_VAISSEAU_4] = {L"Slippy's Arwing", L"L'Arwing de Slippy", L"スリッピーのアーウィン"},
    [T_VAISSEAU_5] = {L"Dash's Arwing", L"L'Arwing de Dash", L"ダッシュのアーウィン"},
    [T_VAISSEAU_6] = {L"Wolf's Wolfen", L"Le Wolfen de Wolf", L"ウルフのウルフェン"},
    [T_VAISSEAU_7] = {L"Leon's Wolfen", L"Le Wolfen de Leon", L"レオンのウルフェン"},
    [T_VAISSEAU_8] = {L"Panther's Wolfen", L"Le Wolfen de Panther", L"パンサーのウルフェン"},
    [T_VAISSEAU_9] = {L"Octoman's ship", L"Le vaisseau d'Octoman", L"オクトマンの機体"},
    [T_VAISSEAU_10] = {L"Bill's ship", L"Le vaisseau de Bill", L"ビルの機体"},
    [T_VAISSEAU_11] = {L"Lucy's ship", L"Le vaisseau de Lucy", L"ルーシーの機体"},
    [T_VAISSEAU_12] = {L"Amanda's ship", L"Le vaisseau d'Amanda", L"アマンダの機体"},
    [T_VAISSEAU_13] = {L"Katt's ship", L"Le vaisseau de Katt", L"キャットの機体"},
    [T_VAISSEAU_14] = {L"Peppy's old Arwing", L"L'ancien Arwing de Peppy", L"ペッピーの旧アーウィン"},
    [T_VAISSEAU_15] = {L"James's old Arwing", L"L'ancien Arwing de James", L"ジェームズの旧アーウィン"},
    [T_VAISSEAU_16] = {L"Jodie's ship", L"Le vaisseau de Jodie", L"ジョディの機体"},
    [T_CELUI_DE_LA_PARTIE] = {L"As in your game", L"Celui de la partie", L"ゲームの進行どおり"},
    [T_LE_SIEN_DE] = {L"Their own (%ls)", L"Le sien (%ls)", L"本来のもの（%ls）"},
    [T_LA_SIENNE_DE] = {L"Their own (%ls)", L"La sienne (%ls)", L"本来のもの（%ls）"},
    [T_LE_SIEN] = {L"Their own", L"Le sien", L"本来のもの"},
    [T_MODIFIE] = {L"%ls  (modified)", L"%ls  (modifié)", L"%ls（変更あり）"},
    [T_DEGATS] = {L"Damage dealt", L"Dégâts infligés", L"与えるダメージ"},
    [T_DEGATS_RECUS] = {L"Damage taken", L"Dégâts reçus", L"受けるダメージ"},
    [T_INVINCIBLE] = {L"Invincible", L"Invincible", L"無敵"},
    [T_TURBO] = {L"Infinite boost", L"Turbo infini", L"ブースト無限"},
    [T_BOMBES] = {L"Infinite bombs", L"Bombes infinies", L"ボム無限"},
    [T_CHARGE] = {L"Always full charge shot", L"Tir chargé au maximum", L"チャージショット常に最大"},
    [T_INFLIGES_1] = {L"Normal", L"Normaux", L"通常"},
    [T_INFLIGES_2] = {L"× 2", L"× 2", L"× 2"},
    [T_INFLIGES_3] = {L"× 4", L"× 4", L"× 4"},
    [T_INFLIGES_4] = {L"× 10", L"× 10", L"× 10"},
    [T_INFLIGES_5] = {L"One hit is enough", L"Un coup suffit", L"一撃で撃破"},
    [T_RECUS_1] = {L"Normal", L"Normaux", L"通常"},
    [T_RECUS_2] = {L"Half as much", L"Moitié moins", L"半分"},
    [T_RECUS_3] = {L"Twice as much", L"Deux fois plus", L"2倍"},
    [T_TITRE_DEGATS] = {L"Damage, for all pilots", L"Les dégâts, pour tous les pilotes", L"ダメージ（全パイロット共通）"},
    [T_TOUS_PILOTES] = {L"For all pilots", L"Pour tous les pilotes", L"全パイロット共通"},
    [T_NOTE_MODS] = {L"All off, it is the game on your ROM. The save keeps none of the ship settings; it keeps the "
                     L"bombs and shield you have left.",
                     L"Éteints, c'est le jeu de la cartouche. La sauvegarde ne garde rien des réglages du vaisseau ; "
                     L"elle garde les bombes et le bouclier qui restent.",
                     L"すべてオフならROMのゲームそのままです。機体の設定はセーブされませんが、残りのボムとシールドは"
                     L"セーブされます。"},
    [T_AUCUN_MOD] = {L"No mods", L"Aucun mod", L"MODをすべてオフ"},

    /* ---- les fenêtres de message ---- */
    [T_FILTRE] = {L"Nintendo DS ROM (*.nds)\0*.nds\0Data folder (index.bin)\0index.bin\0All files\0*.*\0",
                  L"Cartouche Nintendo DS (*.nds)\0*.nds\0Dossier de données (index.bin)\0index.bin\0"
                  L"Tous les fichiers\0*.*\0",
                  L"ニンテンドーDS ROM (*.nds)\0*.nds\0データフォルダー (index.bin)\0index.bin\0"
                  L"すべてのファイル\0*.*\0"},
    [T_TITRE_CHOIX] = {L"Choose your Star Fox Command (Europe) ROM", L"Choisir votre cartouche de Star Fox Command (Europe)",
                       L"「スターフォックス コマンド」（欧州版）のROMを選択"},
    [T_CHOISIR_DABORD] = {L"First choose your Star Fox Command (Europe) ROM — a .nds file — with “Browse…”. The port "
                          L"does not provide it: you need your own.",
                          L"Choisissez d'abord votre cartouche de Star Fox Command (Europe) — un fichier .nds — avec "
                          L"« Parcourir… ». Le portage ne la fournit pas : il faut la vôtre.",
                          L"まず「参照…」で「スターフォックス コマンド」（欧州版）のROM（.ndsファイル）を指定してください。"
                          L"移植版にROMは含まれていません。ご自身のものが必要です。"},
    [T_INTROUVABLE] = {L"This path cannot be found.", L"Ce chemin est introuvable.", L"このパスが見つかりません。"},
    [T_PAS_DONNEES] = {L"This folder is not a data folder: it has no index.bin.",
                       L"Ce dossier n'est pas un dossier de données : il n'a pas d'index.bin.",
                       L"このフォルダーはデータフォルダーではありません（index.bin がありません）。"},
    [T_ILLISIBLE] = {L"This ROM cannot be read.", L"Cette cartouche ne se lit pas.", L"このROMを読み込めません。"},
    [T_AUTRE_VERSION] = {L"This file is not the European Star Fox Command ROM: its game code is %hs, and the port was "
                         L"built on version ASFP — “Star Fox Command (Europe) (En,Fr,De,Es,It)”. Another version "
                         L"would not start.",
                         L"Ce fichier n'est pas la cartouche européenne de Star Fox Command : son code de jeu est %hs, "
                         L"et le portage a été construit sur la version ASFP — « Star Fox Command (Europe) "
                         L"(En,Fr,De,Es,It) ». Une autre version ne démarrerait pas.",
                         L"このファイルは「スターフォックス コマンド」欧州版のROMではありません（ゲームコード：%hs）。"
                         L"この移植版は ASFP 版「Star Fox Command (Europe) (En,Fr,De,Es,It)」を元に作られており、"
                         L"他のバージョンでは起動しません。"},
    [T_PAS_ENCORE] = {L"The game is not built yet: “Build the game” builds it from your ROM.",
                      L"Le jeu n'est pas encore construit : « Préparer le jeu » le construit depuis votre cartouche.",
                      L"ゲームはまだ準備されていません。「ゲームを準備」でROMから構築してください。"},
    [T_PAS_DEMARRE] = {L"The game could not start.", L"Le jeu n'a pas pu démarrer.", L"ゲームを起動できませんでした。"},
    [T_IL_FAUT_NDS] = {L"To build the game you need the ROM itself — the .nds file —, not a data folder.",
                       L"Pour préparer le jeu, il faut la cartouche elle-même — le fichier .nds —, pas un dossier de "
                       L"données.",
                       L"ゲームの準備には、データフォルダーではなくROMファイル（.nds）そのものが必要です。"},
    [T_SANS_PREPARER] = {L"tools\\preparer.py cannot be found next to the launcher: the port is incomplete.",
                         L"tools\\preparer.py est introuvable à côté du lanceur : le portage est incomplet.",
                         L"ランチャーと同じ場所に tools\\preparer.py がありません。移植版のファイルが不足しています。"},
    [T_SANS_PYTHON] = {L"Python cannot be found: it ships in outils\\python next to the launcher, or can be installed "
                       L"separately (python.org, version 3.10 or newer).",
                       L"Python est introuvable : il est livré dans outils\\python à côté du lanceur, ou s'installe "
                       L"à part (python.org, version 3.10 ou plus récente).",
                       L"Pythonが見つかりません。ランチャーと同じ場所の outils\\python に同梱されています。"
                       L"別途インストールすることもできます（python.org、バージョン3.10以降）。"},
    [T_VERSION_PRECEDENTE] = {L"The game was built by an earlier version of the port: it must be built again.\n\n",
                              L"Le jeu a été préparé par une version précédente du portage : il faut le construire à "
                              L"nouveau.\n\n",
                              L"このゲームは以前のバージョンの移植版で準備されたため、もう一度準備する必要があります。\n\n"},
    [T_QUESTION] = {L"%sThe game will be built on this PC, from your ROM: its code is translated, rewritten in C, then "
                    L"compiled. This takes about ten minutes on a recent PC, longer on a modest one, and needs 2 GB of "
                    L"free space during the build. Start?",
                    L"%sLe jeu va être construit sur cette machine, depuis votre cartouche : son code est traduit, "
                    L"réécrit en C puis compilé. Cela prend une dizaine de minutes sur une machine récente, davantage "
                    L"sur une plus modeste, et demande 2 Go libres le temps de la construction. On commence ?",
                    L"%sお持ちのROMから、このPC上でゲームを構築します。コードを変換してCに書き直し、コンパイルします。"
                    L"最近のPCで約10分、性能の控えめなPCではそれ以上かかり、構築中は2GBの空き容量が必要です。"
                    L"開始しますか？"},
    [T_TITRE_PREPARER] = {L"Build the game", L"Préparer le jeu", L"ゲームを準備"},
    [T_PREPARATION_PAS_DEMARREE] = {L"The build could not start.", L"La préparation n'a pas pu démarrer.",
                                    L"準備を開始できませんでした。"},
    [T_ETAPE] = {L"Step %d of %d — %s…", L"Étape %d sur %d — %s…", L"ステップ %d/%d — %s…"},
    [T_ETAPE_1] = {L"the ROM", L"la cartouche", L"ROMの確認"},
    [T_ETAPE_2] = {L"the ROM split into files", L"la cartouche découpée en fichiers", L"ROMをファイルに展開"},
    [T_ETAPE_3] = {L"both processors' code translated to C", L"le code des deux processeurs traduit en C",
                   L"2つのプロセッサのコードをCに変換"},
    [T_ETAPE_4] = {L"the names the binary gives itself", L"les noms que le binaire se donne", L"バイナリ内の名前を収集"},
    [T_ETAPE_5] = {L"each function rewritten in native C", L"chaque fonction réécrite en C natif",
                   L"各関数をネイティブなCに書き直し"},
    [T_ETAPE_6] = {L"compiling (the longest step)", L"la compilation (la plus longue)", L"コンパイル（最も時間がかかります）"},
    [T_REUSSI] = {L"The game is ready: it was built on this PC, from your ROM. It is for your personal use, and is no "
                  L"more shareable than your ROM.",
                  L"Le jeu est prêt : il a été construit sur cette machine, depuis votre cartouche. Il est pour votre "
                  L"usage personnel, et ne se partage pas plus qu'elle.",
                  L"ゲームの準備ができました。お持ちのROMからこのPC上で構築したものです。個人での使用に限り、"
                  L"ROMと同様に他人と共有しないでください。"},
    [T_ECHEC] = {L"The build failed%s%s\n\nThe details are in preparation.log, next to the launcher.",
                 L"La préparation a échoué%s%s\n\nLe détail est dans preparation.log, à côté du lanceur.",
                 L"準備に失敗しました%s%s\n\n詳細はランチャーと同じ場所の preparation.log にあります。"},
    [T_ECHEC_DEUX_POINTS] = {L": ", L" : ", L"："},
    [T_ECHEC_POINT] = {L".", L".", L"。"},
    [T_ARRETER] = {L"The game is being built. Stop? It will have to start over.",
                   L"La préparation du jeu est en cours. L'arrêter ? Il faudra la reprendre du début.",
                   L"ゲームを準備中です。中止しますか？最初からやり直しになります。"},
};

const wchar_t *T(int texte) {
    if (texte < 0 || texte >= NB_TEXTES) return L"";
    const wchar_t *t = TEXTES[texte][g_interface >= 0 && g_interface < NB_INTERFACES ? g_interface : 0];
    return t ? t : TEXTES[texte][0] ? TEXTES[texte][0] : L"";
}

static const wchar_t *const PILOTES[17][NB_INTERFACES] = {
    {L"", L"", L""},
    {L"Fox", L"Fox", L"フォックス"}, {L"Krystal", L"Krystal", L"クリスタル"}, {L"Falco", L"Falco", L"ファルコ"},
    {L"Slippy", L"Slippy", L"スリッピー"}, {L"Dash", L"Dash", L"ダッシュ"}, {L"Wolf", L"Wolf", L"ウルフ"},
    {L"Leon", L"Leon", L"レオン"}, {L"Panther", L"Panther", L"パンサー"}, {L"Octoman", L"Octoman", L"オクトマン"},
    {L"Bill", L"Bill", L"ビル"}, {L"Lucy", L"Lucy", L"ルーシー"}, {L"Amanda", L"Amanda", L"アマンダ"},
    {L"Katt", L"Katt", L"キャット"}, {L"Peppy", L"Peppy", L"ペッピー"}, {L"James", L"James", L"ジェームズ"},
    {L"Jodie", L"Jodie", L"ジョディ"},
};

const wchar_t *nom_pilote(int rang) {
    if (rang < 1 || rang > 16) return L"";
    return PILOTES[rang][g_interface >= 0 && g_interface < NB_INTERFACES ? g_interface : 0];
}

const wchar_t *police_interface(void) { return g_interface == INTERFACE_JA ? L"Yu Gothic UI" : L"Segoe UI"; }

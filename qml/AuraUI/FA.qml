pragma Singleton
import QtQuick

// FontAwesome Free — Solid icon codepoints
// Usage:  Text { font.family: FA.solid; text: FA.cloudArrowUp }
QtObject {

    // ── Font families (must match the name registered in main.cpp / FontLoader) ──
    readonly property string solid:   "Font Awesome 6 Free Solid"
    readonly property string regular: "Font Awesome 6 Free Regular"
    readonly property string brands:  "Font Awesome 6 Brands Regular"

    // ── Sync / Cloud ──────────────────────────────────────────────────────────
    readonly property string cloudArrowUp:    "\uf093"   // upload
    readonly property string cloudArrowDown:  "\uf0ed"   // download
    readonly property string cloud:           "\uf0c2"   // generic cloud
    readonly property string arrowsRotate:    "\uf021"   // syncing / refresh
    readonly property string checkCircle:     "\uf058"   // synced / ok
    readonly property string circleXmark:     "\uf057"   // error
    readonly property string clockRotateLeft: "\uf1da"   // pending / queued

    // ── File actions ──────────────────────────────────────────────────────────
    readonly property string fileArrowUp:     "\uf574"   // upload file
    readonly property string fileArrowDown:   "\uf56d"   // download file
    readonly property string penToSquare:     "\uf044"   // rename / edit
    readonly property string rightLeft:       "\uf362"   // move
    readonly property string trash:           "\uf1f8"   // delete
    readonly property string folderOpen:      "\uf07c"   // folder
    readonly property string file:            "\uf15b"   // generic file
    readonly property string copy:            "\uf0c5"   // copy

    // ── Status / misc ─────────────────────────────────────────────────────────
    readonly property string circleInfo:      "\uf05a"
    readonly property string triangleExclaim: "\uf071"   // warning
    readonly property string lockOpen:        "\uf3c1"
    readonly property string lock:            "\uf023"
    readonly property string userCircle:      "\uf2bd"
    readonly property string gear:            "\uf013"
    readonly property string play:            "\uf04b"
    readonly property string stop:            "\uf04d"
    readonly property string pause:           "\uf04c"
    readonly property string folderPlus:      "\uf65e"
    readonly property string link:            "\uf0c1"
    readonly property string eye:             "\uf06e"
    readonly property string eyeSlash:        "\uf070"
}

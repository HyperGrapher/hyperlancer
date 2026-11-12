pragma Singleton
import QtQuick

// Glyphs of the bundled Phosphor icon font (MIT license). Using a font keeps icons sharp at
// every display scale, and recoloring is a plain color change. Code points come from the
// official @phosphor-icons/web 2.1 style sheet.
QtObject {
    id: icons

    // Navigation and shell
    readonly property string house: ""
    readonly property string squaresFour: ""
    readonly property string pulse: ""
    readonly property string gearSix: ""
    readonly property string globe: ""
    readonly property string binoculars: ""
    readonly property string queue: ""
    readonly property string minus: ""
    readonly property string square: ""
    readonly property string copy: ""
    readonly property string close: ""
    readonly property string arrowLeft: ""
    readonly property string arrowRight: ""
    readonly property string caretRight: ""
    readonly property string caretDown: ""
    readonly property string dotsThree: ""

    // Actions
    readonly property string play: ""
    readonly property string pause: ""
    readonly property string stop: ""
    readonly property string reset: ""
    readonly property string refresh: ""
    readonly property string plus: ""
    readonly property string trash: ""
    readonly property string search: ""
    readonly property string funnel: ""
    readonly property string openExternal: ""
    readonly property string link: ""
    readonly property string folderOpen: ""
    readonly property string fileText: ""
    readonly property string signOut: ""
    readonly property string magicWand: ""

    // States
    readonly property string check: ""
    readonly property string checkCircle: ""
    readonly property string checkSquare: ""
    readonly property string xCircle: ""
    readonly property string warning: ""
    readonly property string warningCircle: ""
    readonly property string info: ""
    readonly property string clock: ""
    readonly property string history: ""
    readonly property string hourglass: ""
    readonly property string spinner: ""
    readonly property string sealCheck: ""

    // Domain
    readonly property string lightning: ""
    readonly property string gauge: ""
    readonly property string code: ""
    readonly property string stack: ""
    readonly property string hardDrives: ""
    readonly property string database: ""
    readonly property string key: ""
    readonly property string plug: ""
    readonly property string plugsConnected: ""
    readonly property string lockSimple: ""
    readonly property string lockOpen: ""
    readonly property string shieldCheck: ""
    readonly property string shieldWarning: ""
    readonly property string certificate: ""
    readonly property string identification: ""
    readonly property string calendar: ""
    readonly property string envelope: ""
    readonly property string at: ""
    readonly property string phone: ""
    readonly property string whatsapp: ""
    readonly property string mapPin: ""
    readonly property string paperPlane: ""
    readonly property string replyArrow: ""
    readonly property string tray: ""
    readonly property string paperclip: ""
    readonly property string googleLogo: ""
    readonly property string facebook: ""
    readonly property string instagram: ""
    readonly property string linkedin: ""
    readonly property string youtube: ""
    readonly property string xLogo: ""
    readonly property string chartBar: ""
    readonly property string terminal: ""
    readonly property string browser: ""
    readonly property string deviceMobile: ""
    readonly property string paintBrush: ""
    readonly property string article: ""
    readonly property string tag: ""
    readonly property string target: ""
    readonly property string trendUp: ""
    readonly property string cookie: ""
    readonly property string robot: ""
    readonly property string wrench: ""
    readonly property string lightbulb: ""
    readonly property string user: ""
    readonly property string users: ""
    readonly property string eye: ""
    readonly property string eyeSlash: ""

    // Appearance settings
    readonly property string sparkle: ""
    readonly property string moon: ""
    readonly property string drop: ""
    readonly property string palette: ""
    readonly property string waveform: ""
    readonly property string sliders: ""
    readonly property string cube: ""

    function named(name) {
        const glyph = icons[name];
        return typeof glyph === "string" ? glyph : cube;
    }
}

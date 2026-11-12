pragma Singleton
import QtQuick

// Text formatting shared by every page: relative times, dates, days left, numbers.
QtObject {
    // Lead statuses in pipeline order; the same list as SiteOrganizer::statuses().
    readonly property var leadStatusOptions: ["none", "consider", "design", "contacted", "replied",
                                              "client", "completed", "canceled"]
        .map(status => ({ value: status, label: leadStatusLabel(status), tint: leadStatusColor(status) }))

    function leadStatusLabel(status) {
        switch (status) {
        case "none": return qsTr("None");
        case "consider": return qsTr("Consider");
        case "design": return qsTr("Design");
        case "contacted": return qsTr("Contacted");
        case "replied": return qsTr("Replied");
        case "client": return qsTr("Client");
        case "completed": return qsTr("Completed");
        case "canceled": return qsTr("Canceled");
        default: return status;
        }
    }

    function leadStatusColor(status) {
        switch (status) {
        case "consider": return Theme.info;
        case "design": return Theme.accent;
        case "contacted": return Theme.warning;
        case "replied": return Theme.accentSecondary;
        case "client": return Theme.success;
        case "completed": return "#7CF5C8";
        case "canceled": return Theme.danger;
        default: return Theme.textMuted;
        }
    }

    function relativeTime(iso) {
        if (!iso)
            return "";
        const then = new Date(iso);
        if (isNaN(then.getTime()))
            return "";
        const seconds = Math.round((Date.now() - then.getTime()) / 1000);
        if (seconds < 45)
            return qsTr("just now");
        const minutes = Math.round(seconds / 60);
        if (minutes < 60)
            return qsTr("%1 min ago").arg(minutes);
        const hours = Math.round(minutes / 60);
        if (hours < 24)
            return qsTr("%1 h ago").arg(hours);
        const days = Math.round(hours / 24);
        if (days < 30)
            return days === 1 ? qsTr("yesterday") : qsTr("%1 days ago").arg(days);
        return shortDate(iso);
    }

    function shortDate(value) {
        if (!value)
            return "";
        const date = new Date(value);
        if (isNaN(date.getTime()))
            return String(value);
        return date.toLocaleDateString(Qt.locale(), "d MMM yyyy");
    }

    function dateTime(value) {
        if (!value)
            return "";
        const date = new Date(value);
        return isNaN(date.getTime()) ? String(value) : date.toLocaleString(Qt.locale(), "d MMM yyyy, HH:mm");
    }

    // Days from today until a YYYY-MM-DD or ISO date; null when unknown.
    function daysUntil(value) {
        if (!value)
            return null;
        const date = new Date(String(value).length === 10 ? value + "T00:00:00" : value);
        if (isNaN(date.getTime()))
            return null;
        const today = new Date();
        today.setHours(0, 0, 0, 0);
        return Math.round((date.getTime() - today.getTime()) / 86400000);
    }

    function daysLabel(days) {
        if (days === null || days === undefined)
            return "—";
        if (days < 0)
            return qsTr("expired");
        if (days === 0)
            return qsTr("today");
        return qsTr("%1 d").arg(days);
    }

    // For to-dos and deadlines, where a past date means overdue rather than expired.
    function dueLabel(days) {
        if (days === null || days === undefined)
            return "—";
        if (days < 0)
            return qsTr("%1 d overdue").arg(-days);
        return days === 0 ? qsTr("today") : qsTr("%1 d").arg(days);
    }

    function dueColor(days) {
        if (days === null || days === undefined)
            return Theme.textMuted;
        return days <= 0 ? Theme.danger : days <= 3 ? Theme.warning : Theme.textSecondary;
    }

    function daysColor(days) {
        if (days === null || days === undefined)
            return Theme.textMuted;
        if (days <= 14)
            return Theme.danger;
        if (days <= 30)
            return Theme.warning;
        return Theme.textSecondary;
    }

    function bytes(value) {
        if (value === null || value === undefined)
            return "—";
        if (value < 1024)
            return value + " B";
        if (value < 1024 * 1024)
            return (value / 1024).toFixed(1) + " KB";
        return (value / 1024 / 1024).toFixed(1) + " MB";
    }

    function ms(value) {
        if (value === null || value === undefined)
            return "—";
        return value >= 1000 ? (value / 1000).toFixed(2) + " s" : Math.round(value) + " ms";
    }

    function orDash(value) {
        return value === null || value === undefined || value === "" ? "—" : String(value);
    }

    function yesNo(value) {
        if (value === null || value === undefined)
            return "—";
        return value ? qsTr("Yes") : qsTr("No");
    }

    // Stable hue (0..359) for a text, used to tint a site's initial badge.
    function hueOf(text) {
        let hash = 0;
        const value = String(text || "");
        for (let i = 0; i < value.length; ++i)
            hash = (hash * 31 + value.charCodeAt(i)) % 360;
        return hash;
    }

    function initialOf(text) {
        const value = String(text || "?").replace(/^www\./, "");
        return value.length > 0 ? value.charAt(0).toUpperCase() : "?";
    }
}

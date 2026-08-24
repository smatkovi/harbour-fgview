import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.fgview 1.0
import "pages"
import "pages"

ApplicationWindow {
    id: app
    allowedOrientations: Orientation.Landscape
    _defaultPageOrientations: Orientation.Landscape

    FgRuntime { id: fgRuntime }
    initialPage: Component { StartPage { rt: fgRuntime } }
    cover: Component {
        CoverBackground {
            CoverPlaceholder {
                icon.source: "image://theme/icon-l-play"
                text: "FlightGear"
            }
        }
    }
}

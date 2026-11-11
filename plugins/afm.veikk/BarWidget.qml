import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io
import qs.Ui
import qs.Commons
import qs.Commons as Commons

Panel {
  id: root
  moduleName: "afm.veikk"
  ipcTarget: "afm.veikk"

  // Como los indicadores de Omarchy (luz nocturna, mantener activo...): se ve
  // mientras el servicio está activo; si no, queda oculto y solo aparece, atenuado,
  // al pasar el puntero por la barra, o mientras el panel está abierto.
  readonly property bool alwaysShow: setting("alwaysShow", false) === true
  readonly property bool barRevealed: !!bar && bar.centerSectionRevealHeld === true
    && bar.centerHoverRevealSuppressed !== true
  readonly property bool shown: running || alwaysShow || barRevealed || opened

  implicitWidth: button.visible ? button.implicitWidth : 0
  implicitHeight: button.implicitHeight

  readonly property string configPath: Quickshell.env("HOME") + "/.config/veikk/config.json"

  // Estado del servicio
  property bool running: false
  property bool serviceBusy: false
  property string problem: ""
  property string deviceName: ""

  // Área de trabajo en porcentaje (0-100) de la superficie de la tableta
  property real areaX: 0
  property real areaY: 0
  property real areaW: 100
  property real areaH: 100
  property bool keepProportions: false
  property real tabletAspect: 1
  property string monitor: ""
  property var monitors: []
  // Tamaño físico (mm) de cada pantalla, leído del EDID: { "DP-1": { w: 597, h: 336 } }
  property var monitorMm: ({})
  // Relación de aspecto del conjunto de pantallas (cuando no hay una elegida).
  property real layoutAspect: 16 / 9

  readonly property real tabletMmW: Math.max(1, Number(setting("tabletWidthMm", 152.4)))
  readonly property real tabletMmH: Math.max(1, Number(setting("tabletHeightMm", 101.6)))
  readonly property real physicalAspect: tabletMmW / tabletMmH

  readonly property color foreground: bar ? bar.foreground : Commons.Color.foreground
  readonly property color dim: Qt.darker(foreground, 1.55)
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family

  function refresh() {
    if (!statusProc.running) statusProc.running = true
    if (!configProc.running && !saveProc.running && !dragging) configProc.running = true
    if (!monitorsProc.running) monitorsProc.running = true
    if (!edidProc.running) edidProc.running = true
  }

  property bool dragging: false

  function setService(on) {
    if (serviceBusy) return
    serviceBusy = true
    serviceProc.command = ["systemctl", "--user", on ? "start" : "stop", "veikk.service"]
    serviceProc.running = true
  }

  // Relación ancho/alto de la pantalla elegida.
  function monitorAspect() {
    if (monitor === "") return layoutAspect
    for (var i = 0; i < monitors.length; i++) {
      if (monitors[i].name === monitor) return monitors[i].aspect
    }
    return 16 / 9
  }

  // Alto (en %) que corresponde a un ancho (en %) para igualar la proporción
  // de la pantalla.
  function proportionalH(w) { return w * tabletAspect / monitorAspect() }
  function maxProportionalW() { return Math.min(100, 100 * monitorAspect() / tabletAspect) }

  // Cuánto se deforma un círculo: 0 = nada, 1 = el doble.
  function distortion() {
    var aw = areaW / 100 * tabletAspect
    var ah = areaH / 100
    if (ah <= 0) return 0
    return Math.abs((aw / ah) / monitorAspect() - 1)
  }

  // Área con la proporción de la pantalla, lo más grande posible y centrada.
  function fitToMonitor() {
    areaW = Math.min(100, maxProportionalW())
    areaH = proportionalH(areaW)
    areaX = (100 - areaW) / 2
    areaY = (100 - areaH) / 2
    clampArea()
    save()
  }

  // Pantalla elegida en mm, o null si el EDID no lo indica.
  function currentMonitorMm() {
    var m = monitorMm[monitor]
    return m && m.w > 0 && m.h > 0 ? m : null
  }

  // Factor pantalla/tableta en cada eje (1 mm dibujado = N mm en pantalla).
  function scaleX() {
    var m = currentMonitorMm()
    return m ? m.w / Math.max(0.0001, areaW / 100 * tabletMmW) : 0
  }
  function scaleY() {
    var m = currentMonitorMm()
    return m ? m.h / Math.max(0.0001, areaH / 100 * tabletMmH) : 0
  }

  readonly property bool realMode: keepProportions
    && Math.abs(tabletAspect - physicalAspect) < 0.01 && distortion() < 0.03

  // Modo real: usa la proporción física de la tableta, el mismo factor en ambos
  // ejes y el área más grande posible, centrada.
  function applyRealMode() {
    tabletAspect = physicalAspect
    keepProportions = true
    fitToMonitor()
  }

  function clampArea() {
    areaW = Math.max(5, Math.min(100, areaW))
    areaH = Math.max(5, Math.min(100, areaH))
    areaX = Math.max(0, Math.min(100 - areaW, areaX))
    areaY = Math.max(0, Math.min(100 - areaH, areaY))
  }

  function applyProportions() {
    if (keepProportions) {
      areaW = Math.min(areaW, maxProportionalW())
      areaH = proportionalH(areaW)
    }
    clampArea()
  }

  function save() {
    var patch = {
      area: {
        x: Math.round(areaX * 100) / 10000,
        y: Math.round(areaY * 100) / 10000,
        w: Math.round(areaW * 100) / 10000,
        h: Math.round(areaH * 100) / 10000
      },
      keepProportions: keepProportions,
      tabletAspect: tabletAspect
    }
    patch.monitor = monitor
    saveProc.command = ["sh", "-c",
      't=$(mktemp) && jq --argjson p "$1" ". * \$p" "$2" > "$t" && cat "$t" > "$2"; rm -f "$t"',
      "sh", JSON.stringify(patch), configPath]
    saveProc.running = true
  }

  function resetArea() {
    areaX = 0; areaY = 0; areaW = 100; areaH = 100
    applyProportions()
    save()
  }

  function setKeepProportions(on) {
    keepProportions = on
    applyProportions()
    save()
  }

  // Al cambiar de pantalla con las proporciones activas, el área se vuelve a
  // ajustar a la nueva pantalla (más grande posible y centrada).
  function setMonitor(name) {
    monitor = name
    if (keepProportions) fitToMonitor()
    else save()
  }

  onOpenedChanged: if (opened) refresh()
  Component.onCompleted: refresh()

  Timer {
    interval: Math.max(2, root.setting("refreshIntervalSec", 5)) * 1000
    running: true
    repeat: true
    onTriggered: root.refresh()
  }

  Process {
    id: statusProc
    command: ["veikk", "status", "--json"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        try {
          var s = JSON.parse(String(text || "{}"))
          root.running = s.daemonRunning === true
          root.problem = String(s.problem || "").slice(0, 200)
          var name = ""
          var devices = s.devices instanceof Array ? s.devices : []
          for (var i = 0; i < devices.length; i++) {
            if (devices[i].pen === true && devices[i].virtual !== true) { name = String(devices[i].name || ""); break }
          }
          root.deviceName = name.slice(0, 80)
        } catch (e) {
          root.running = false
        }
      }
    }
    onExited: function(code) { if (code !== 0) root.running = false }
  }

  Process {
    id: configProc
    command: ["jq", "-c", "{area, keepProportions, tabletAspect, monitor}", root.configPath]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        try {
          var c = JSON.parse(String(text || "{}"))
          var a = c.area || {}
          function pct(v, d) { v = Number(v); return isFinite(v) ? Math.max(0, Math.min(100, v * 100)) : d }
          var aspect = Number(c.tabletAspect)
          root.tabletAspect = isFinite(aspect) && aspect > 0 ? aspect : 1
          root.keepProportions = c.keepProportions === true
          root.monitor = String(c.monitor || "").slice(0, 64)
          root.areaW = Math.max(5, pct(a.w, 100))
          root.areaH = Math.max(5, pct(a.h, 100))
          root.areaX = Math.min(pct(a.x, 0), 100 - root.areaW)
          root.areaY = Math.min(pct(a.y, 0), 100 - root.areaH)
        } catch (e) {}
      }
    }
  }

  Process {
    id: monitorsProc
    command: ["hyprctl", "monitors", "-j"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        try {
          var list = JSON.parse(String(text || "[]"))
          var result = []
          var minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity
          for (var i = 0; i < list.length && i < 8; i++) {
            var m = list[i]
            var w = Number(m.width), h = Number(m.height)
            if (!isFinite(w) || !isFinite(h) || w <= 0 || h <= 0) continue
            if (Number(m.transform) % 2 === 1) { var t = w; w = h; h = t }
            result.push({ name: String(m.name || "").slice(0, 64), aspect: w / h })
            var sc = Number(m.scale) > 0 ? Number(m.scale) : 1
            var mx = Number(m.x) || 0, my = Number(m.y) || 0
            minX = Math.min(minX, mx); minY = Math.min(minY, my)
            maxX = Math.max(maxX, mx + w / sc); maxY = Math.max(maxY, my + h / sc)
          }
          if (result.length > 0 && maxY > minY) root.layoutAspect = (maxX - minX) / (maxY - minY)
          root.monitors = result
        } catch (e) {}
      }
    }
  }

  Process {
    id: edidProc
    command: ["sh", "-c",
      'for f in /sys/class/drm/card*-*/edid; do [ -r "$f" ] || continue; n=${f%/edid}; n=${n##*/card[0-9]-}; set -- $(od -An -tu1 -j66 -N3 "$f" 2>/dev/null); [ -n "$3" ] || continue; echo "$n $(( ($3 / 16) * 256 + $1 )) $(( ($3 % 16) * 256 + $2 ))"; done']
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        var result = {}
        var lines = String(text || "").split("\n")
        for (var i = 0; i < lines.length && i < 16; i++) {
          var p = lines[i].trim().split(" ")
          if (p.length !== 3) continue
          var w = Number(p[1]), h = Number(p[2])
          if (isFinite(w) && isFinite(h) && w > 0 && h > 0) result[p[0].slice(0, 64)] = { w: w, h: h }
        }
        root.monitorMm = result
      }
    }
  }

  Process { id: saveProc }

  Process {
    id: serviceProc
    onRunningChanged: if (!running) { root.serviceBusy = false; root.refresh() }
  }

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: "󰏪"
    active: root.running
    hasVisualContent: root.shown
    dimmed: !root.running
    concealed: !root.shown
    interactive: root.shown
    tooltipText: root.running ? "Veikk: activo" : "Veikk: inactivo"
    onPressed: function(b) {
      if (b === Qt.RightButton) root.setService(!root.running)
      else root.toggle()
    }
  }

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: panel.loadedContent
    contentWidth: panel.fittedContentWidth(Style.space(360))
    contentHeight: panel.fittedContentHeight(panel.loadedContentHeight, Style.space(800))

    content: PanelKeyCatcher {
      anchors.fill: parent
      implicitHeight: column.implicitHeight
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }

      ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

      Column {
        id: column
        width: scroll.availableWidth
        spacing: Style.space(12)

        // Cabecera: estado + interruptor del servicio
        Item {
          width: parent.width
          implicitHeight: Math.max(heroLabels.implicitHeight, serviceSwitch.implicitHeight)

          Column {
            id: heroLabels
            anchors.left: parent.left
            anchors.right: serviceSwitch.left
            anchors.rightMargin: Style.space(10)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Style.space(2)

            Text {
              width: parent.width
              textFormat: Text.PlainText
              text: "Veikk"
              color: root.foreground
              font.family: root.fontFamily
              font.pixelSize: Style.font.title
              font.bold: true
              elide: Text.ElideRight
            }
            Text {
              width: parent.width
              textFormat: Text.PlainText
              text: root.running ? "SERVICIO ACTIVO" : "SERVICIO DETENIDO"
              color: root.running ? root.foreground : root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              font.bold: true
              font.letterSpacing: 1.2
              elide: Text.ElideRight
            }
            Text {
              width: parent.width
              visible: text !== ""
              textFormat: Text.PlainText
              text: root.problem !== "" ? root.problem : root.deviceName
              color: root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              elide: Text.ElideRight
            }
          }

          ToggleSwitch {
            id: serviceSwitch
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            foreground: root.foreground
            checked: root.running
            busy: root.serviceBusy
            onToggled: root.setService(!root.running)
          }
        }

        PanelSeparator { foreground: root.foreground }

        PanelSectionHeader {
          text: "ÁREA DE TRABAJO"
          foreground: root.foreground
          fontFamily: root.fontFamily
        }

        // Vista previa: arrastra el área para moverla, o su esquina para cambiar el tamaño.
        Item {
          id: preview
          width: parent.width
          height: Style.space(130)

          readonly property real tw: Math.min(width * 0.42, height * root.tabletAspect)
          readonly property real th: tw / root.tabletAspect
          readonly property real mw: Math.min(width * 0.46, height * root.monitorAspect())
          readonly property real mh: mw / root.monitorAspect()
          readonly property real grip: Style.space(16)

          Rectangle {
            id: tablet
            width: preview.tw
            height: preview.th
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            color: Style.selectedFillFor(root.foreground, Commons.Color.accent)
            border.color: root.dim
            border.width: 1
            radius: Style.cornerRadius > 0 ? Style.space(4) : 0

            Rectangle {
              id: areaRect
              x: root.areaX / 100 * tablet.width
              y: root.areaY / 100 * tablet.height
              width: root.areaW / 100 * tablet.width
              height: root.areaH / 100 * tablet.height
              color: Qt.rgba(Commons.Color.accent.r, Commons.Color.accent.g, Commons.Color.accent.b, 0.35)
              border.color: Commons.Color.accent
              border.width: 2

              Canvas {
                id: penCircle
                anchors.fill: parent
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                  var ctx = getContext("2d")
                  ctx.reset()
                  var r = Math.min(width, height) * 0.3
                  ctx.strokeStyle = Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.9)
                  ctx.lineWidth = 1.5
                  ctx.beginPath()
                  ctx.ellipse(width / 2 - r, height / 2 - r, r * 2, r * 2)
                  ctx.stroke()
                }
              }

              Rectangle {
                width: preview.grip / 2
                height: width
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                color: Commons.Color.accent
              }
            }

            MouseArea {
              id: dragArea
              anchors.fill: parent
              hoverEnabled: true
              property string mode: ""
              property real offX: 0
              property real offY: 0

              function inGrip(mx, my) {
                return mx >= areaRect.x + areaRect.width - preview.grip && mx <= areaRect.x + areaRect.width + 2
                    && my >= areaRect.y + areaRect.height - preview.grip && my <= areaRect.y + areaRect.height + 2
              }
              function inArea(mx, my) {
                return mx >= areaRect.x && mx <= areaRect.x + areaRect.width && my >= areaRect.y && my <= areaRect.y + areaRect.height
              }
              function update(mx, my) {
                var px = mx / tablet.width * 100
                var py = my / tablet.height * 100
                if (mode === "move") {
                  root.areaX = Math.max(0, Math.min(100 - root.areaW, px - offX))
                  root.areaY = Math.max(0, Math.min(100 - root.areaH, py - offY))
                } else if (mode === "resize") {
                  if (root.keepProportions) {
                    var ratio = root.proportionalH(1)
                    var w = Math.max(5, px - root.areaX)
                    w = Math.min(w, 100 - root.areaX, (100 - root.areaY) / ratio, root.maxProportionalW())
                    root.areaW = w
                    root.areaH = w * ratio
                  } else {
                    root.areaW = Math.max(5, Math.min(100 - root.areaX, px - root.areaX))
                    root.areaH = Math.max(5, Math.min(100 - root.areaY, py - root.areaY))
                  }
                }
              }

              cursorShape: mode === "" && containsMouse ? (inGrip(mouseX, mouseY) ? Qt.SizeFDiagCursor : (inArea(mouseX, mouseY) ? Qt.SizeAllCursor : Qt.ArrowCursor)) : (mode === "resize" ? Qt.SizeFDiagCursor : Qt.SizeAllCursor)

              onPressed: function(m) {
                root.dragging = true
                if (inGrip(m.x, m.y)) mode = "resize"
                else {
                  mode = "move"
                  if (inArea(m.x, m.y)) {
                    offX = m.x / tablet.width * 100 - root.areaX
                    offY = m.y / tablet.height * 100 - root.areaY
                  } else {
                    // Clic fuera: centra el área en el punto.
                    offX = root.areaW / 2
                    offY = root.areaH / 2
                    update(m.x, m.y)
                  }
                }
              }
              onPositionChanged: function(m) { if (pressed) update(m.x, m.y) }
              onReleased: {
                mode = ""
                root.dragging = false
                root.save()
              }
              onCanceled: { mode = ""; root.dragging = false }
            }
          }

          Text {
            anchors.horizontalCenter: undefined
            x: tablet.x + tablet.width + (monitorBox.x - tablet.x - tablet.width) / 2 - width / 2
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: "→"
            color: root.dim
            font.family: root.fontFamily
            font.pixelSize: Style.font.title
          }

          Rectangle {
            id: monitorBox
            width: preview.mw
            height: preview.mh
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            color: Qt.rgba(Commons.Color.accent.r, Commons.Color.accent.g, Commons.Color.accent.b, 0.35)
            border.color: Commons.Color.accent
            border.width: 2

            // El área completa se estira sobre toda la pantalla; el círculo muestra la deformación.
            Canvas {
              id: monitorCircle
              anchors.fill: parent
              onWidthChanged: requestPaint()
              onHeightChanged: requestPaint()
              Connections {
                target: root
                function onAreaWChanged() { monitorCircle.requestPaint() }
                function onAreaHChanged() { monitorCircle.requestPaint() }
                function onTabletAspectChanged() { monitorCircle.requestPaint() }
              }
              onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                // Radio del círculo en la tableta, en unidades de tableta (alto = 1).
                var aw = root.areaW / 100 * root.tabletAspect
                var ah = root.areaH / 100
                var r = Math.min(aw, ah) * 0.3
                var rx = r / aw * width
                var ry = r / ah * height
                ctx.strokeStyle = Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.9)
                ctx.lineWidth = 1.5
                ctx.beginPath()
                ctx.ellipse(width / 2 - rx, height / 2 - ry, rx * 2, ry * 2)
                ctx.stroke()
              }
            }
          }
        }

        Text {
          width: parent.width
          wrapMode: Text.WordWrap
          textFormat: Text.PlainText
          text: root.distortion() > 0.03
            ? "El área se estira sobre toda la pantalla: un círculo dibujado se verá como elipse. Usa «Ajustar a pantalla» para corregirlo."
            : "El área de la tableta (recuadro) ocupa toda la pantalla. El círculo se ve circular."
          color: root.dim
          font.family: root.fontFamily
          font.pixelSize: Style.font.caption
        }

        // Pantalla de destino
        PanelSectionHeader {
          visible: root.monitors.length > 0
          text: "PANTALLA"
          foreground: root.foreground
          fontFamily: root.fontFamily
        }

        Flow {
          visible: root.monitors.length > 0
          width: parent.width
          spacing: Style.space(6)
          Button {
            text: "Todas"
            foreground: root.foreground
            fontFamily: root.fontFamily
            bordered: true
            selected: root.monitor === ""
            onClicked: root.setMonitor("")
          }
          Repeater {
            model: root.monitors
            delegate: Button {
              required property var modelData
              text: modelData.name
              foreground: root.foreground
              fontFamily: root.fontFamily
              bordered: true
              selected: modelData.name === root.monitor
              onClicked: root.setMonitor(modelData.name)
            }
          }
        }

        Toggle {
          width: parent.width
          label: "Modo real"
          description: "Respeta el tamaño físico de la tableta: el mismo factor en ancho y alto"
          fontFamily: root.fontFamily
          checked: root.realMode
          onClicked: root.realMode ? root.setKeepProportions(false) : root.applyRealMode()
        }

        Text {
          width: parent.width
          visible: root.currentMonitorMm() !== null
          wrapMode: Text.WordWrap
          textFormat: Text.PlainText
          text: {
            var sx = root.scaleX(), sy = root.scaleY()
            var fx = Commons.I18n.formatNumber(sx, 1), fy = Commons.I18n.formatNumber(sy, 1)
            if (Math.abs(sx - sy) / Math.max(sx, sy, 0.0001) < 0.03)
              return "Escala: 1 cm dibujado en la tableta = " + fx + " cm en pantalla."
            return "Escala: ×" + fx + " en horizontal y ×" + fy + " en vertical (el dibujo se deforma)."
          }
          color: root.dim
          font.family: root.fontFamily
          font.pixelSize: Style.font.caption
        }

        Toggle {
          width: parent.width
          label: "Mantener proporciones"
          description: "Ajusta el alto a la proporción de la pantalla"
          fontFamily: root.fontFamily
          checked: root.keepProportions
          onClicked: root.setKeepProportions(!root.keepProportions)
        }

        PanelSeparator { foreground: root.foreground }

        Repeater {
          model: [
            { key: "w", label: "Ancho" },
            { key: "h", label: "Alto" },
            { key: "x", label: "Posición X" },
            { key: "y", label: "Posición Y" }
          ]

          delegate: Column {
            id: row
            required property var modelData
            width: column.width
            spacing: Style.space(2)
            enabled: !(modelData.key === "h" && root.keepProportions)
            opacity: enabled ? 1 : 0.45

            readonly property real current: modelData.key === "w" ? root.areaW
              : modelData.key === "h" ? root.areaH
              : modelData.key === "x" ? root.areaX : root.areaY
            readonly property real upper: modelData.key === "w" ? (root.keepProportions ? Math.min(100, root.maxProportionalW(), 100 / Math.max(0.01, root.proportionalH(1))) : 100)
              : modelData.key === "h" ? 100
              : modelData.key === "x" ? 100 - root.areaW : 100 - root.areaH
            readonly property real lower: modelData.key === "w" || modelData.key === "h" ? 5 : 0

            Item {
              width: parent.width
              implicitHeight: Math.max(rowLabel.implicitHeight, rowValue.implicitHeight)
              Text {
                id: rowLabel
                anchors.left: parent.left
                textFormat: Text.PlainText
                text: row.modelData.label
                color: root.foreground
                font.family: root.fontFamily
                font.pixelSize: Style.font.body
              }
              Text {
                id: rowValue
                anchors.right: parent.right
                anchors.rightMargin: Style.space(6)
                textFormat: Text.PlainText
                text: Commons.I18n.formatNumber(Math.round(slider.dragging ? slider.liveValue : row.current)) + "%"
                color: root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
                font.bold: true
              }
            }

            PanelSlider {
              id: slider
              bar: root.bar
              width: parent.width
              minimum: row.lower
              maximum: Math.max(row.lower + 1, row.upper)
              step: 1
              integer: true
              value: row.current
              onMoved: function(v) {
                var k = row.modelData.key
                if (k === "w") { root.areaW = v; if (root.keepProportions) root.areaH = root.proportionalH(v); root.clampArea() }
                else if (k === "h") { root.areaH = v; root.clampArea() }
                else if (k === "x") root.areaX = v
                else root.areaY = v
              }
              onReleased: function(v) { root.save() }
            }
          }
        }

        PanelSeparator { foreground: root.foreground }

        Flow {
          width: parent.width
          spacing: Style.space(8)
          Button {
            text: "Ajustar a pantalla"
            foreground: root.foreground
            fontFamily: root.fontFamily
            bordered: true
            onClicked: root.fitToMonitor()
          }
          Button {
            text: "Restablecer"
            foreground: root.foreground
            fontFamily: root.fontFamily
            bordered: true
            onClicked: root.resetArea()
          }
          Button {
            text: "Abrir Veikk"
            foreground: root.foreground
            fontFamily: root.fontFamily
            bordered: true
            onClicked: { Quickshell.execDetached(["veikk", "open"]); root.close() }
          }
        }
      }
      }
    }
  }
}

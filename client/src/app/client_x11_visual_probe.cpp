#include "client_x11_visual_probe.h"

#include "media/ClientVideoDiagCache.h"

#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QQuickWindow>
#include <QTimer>
#include <QWindow>

#include <cstdlib>
#include <cstring>

#include <QtGlobal>

#if defined(CLIENT_HAVE_XCB)
#include <qnativeinterface.h>

#include <xcb/xcb.h>
#endif

namespace {

bool visualProbeDisabled() {
  const QString v =
      QProcessEnvironment::systemEnvironment().value(QStringLiteral("CLIENT_X11_VISUAL_PROBE")).trimmed();
  return v == QLatin1String("0") || v.toLower() == QLatin1String("false") ||
         v.toLower() == QLatin1String("off");
}

bool shouldSkipPlatform(QGuiApplication *app) {
  if (!app) {
    return true;
  }
  const QString plat = app->platformName().toLower();
  if (plat != QLatin1String("xcb")) {
    return true;
  }
  if (!QGuiApplication::primaryScreen()) {
    return true;
  }
  return false;
}

#if defined(CLIENT_HAVE_XCB)

const char *visualClassName(uint8_t c) {
  switch (c) {
  case XCB_VISUAL_CLASS_STATIC_GRAY:
    return "StaticGray";
  case XCB_VISUAL_CLASS_GRAY_SCALE:
    return "GrayScale";
  case XCB_VISUAL_CLASS_STATIC_COLOR:
    return "StaticColor";
  case XCB_VISUAL_CLASS_PSEUDO_COLOR:
    return "PseudoColor";
  case XCB_VISUAL_CLASS_TRUE_COLOR:
    return "TrueColor";
  case XCB_VISUAL_CLASS_DIRECT_COLOR:
    return "DirectColor";
  default:
    return "Unknown";
  }
}

bool lookupVisualProps(xcb_connection_t *conn, xcb_visualid_t visual, uint8_t *outDepth,
                       const xcb_visualtype_t **outVt) {
  const xcb_setup_t *setup = xcb_get_setup(conn);
  xcb_screen_iterator_t sit = xcb_setup_roots_iterator(setup);
  for (; sit.rem; xcb_screen_next(&sit)) {
    xcb_depth_iterator_t dit = xcb_screen_allowed_depths_iterator(sit.data);
    for (; dit.rem; xcb_depth_next(&dit)) {
      xcb_depth_t *depth = dit.data;
      xcb_visualtype_iterator_t vit = xcb_depth_visuals_iterator(depth);
      for (; vit.rem; xcb_visualtype_next(&vit)) {
        if (vit.data->visual_id == visual) {
          *outDepth = depth->depth;
          *outVt = vit.data;
          return true;
        }
      }
    }
  }
  return false;
}

void logOneQuickWindow(QQuickWindow *qw, xcb_connection_t *conn) {
  if (!qw || !conn) {
    return;
  }
  const WId wid = qw->winId();
  if (!wid) {
    qInfo().noquote() << "[Client][X11Visual] title=\"" << qw->title() << "\" winId=0（下一事件循环再试）";
    return;
  }

  const xcb_window_t xw = static_cast<xcb_window_t>(wid);
  xcb_get_window_attributes_cookie_t ck = xcb_get_window_attributes(conn, xw);
  xcb_get_window_attributes_reply_t *attr = xcb_get_window_attributes_reply(conn, ck, nullptr);
  if (!attr) {
    qWarning().noquote() << "[Client][X11Visual] xcb_get_window_attributes 失败 xid=0x"
                         << QString::number(static_cast<quintptr>(wid), 16);
    return;
  }

  const xcb_visualid_t visual = attr->visual;
  const xcb_colormap_t cmap = attr->colormap;
  const uint8_t winClass = attr->_class;
  std::free(attr);  // xcb reply from malloc

  uint8_t depth = 0;
  const xcb_visualtype_t *vt = nullptr;
  const bool found = lookupVisualProps(conn, visual, &depth, &vt);

  QString argbHint = QStringLiteral("unknown");
  if (found && vt) {
    // 32 位深度 TrueColor 常见于带 alpha 的窗口；24 位通常为 RGB888 客户区（是否透底还看 Qt/混成器）
    if (depth >= 32 && vt->_class == XCB_VISUAL_CLASS_TRUE_COLOR) {
      argbHint = QStringLiteral("likely_rgba_visual(depth>=32+TrueColor)");
    } else if (depth == 24 && vt->_class == XCB_VISUAL_CLASS_TRUE_COLOR) {
      argbHint = QStringLiteral("typical_rgb888_no_alpha_in_visual(depth=24)");
    } else {
      argbHint = QStringLiteral("see_depth_class");
    }
  } else {
    argbHint = QStringLiteral("visual_lookup_failed");
  }

  QString vclassStr = QStringLiteral("?");
  uint8_t bprgb = 0;
  QString masks = QStringLiteral("-");
  if (found && vt) {
    vclassStr = QString::fromUtf8(visualClassName(vt->_class));
    bprgb = vt->bits_per_rgb_value;
    masks = QStringLiteral("0x%1/0x%2/0x%3")
                .arg(vt->red_mask, 0, 16)
                .arg(vt->green_mask, 0, 16)
                .arg(vt->blue_mask, 0, 16);
  }

  qInfo().nospace().noquote()
      << "[Client][X11Visual] title=\"" << qw->title() << "\""
      << " xid=0x" << QString::number(static_cast<quintptr>(wid), 16)
      << " windowClass=" << int(winClass) << "(1=InputOutput)"
      << " visualId=0x" << QString::number(visual, 16) << " depth=" << int(depth)
      << " visualType=" << vclassStr << " bitsRGB=" << int(bprgb) << " rgbMasks=" << masks
      << " colormap=0x" << QString::number(cmap, 16) << " argbHeuristic=" << argbHint
      << " | 透底排查: 对照 depth/visual、CLIENT_GL_ALPHA_BUFFER_SIZE、混成器；取证 "
         "CLIENT_XWININFO_SNAPSHOT=1";
}

bool wmProbeDisabled() {
  const QString v =
      QProcessEnvironment::systemEnvironment().value(QStringLiteral("CLIENT_X11_WM_PROBE")).trimmed();
  return v == QLatin1String("0") || v.toLower() == QLatin1String("false") ||
         v.toLower() == QLatin1String("off");
}

xcb_atom_t internAtomName(xcb_connection_t *conn, const char *name) {
  const auto len = static_cast<uint16_t>(std::strlen(name));
  xcb_intern_atom_cookie_t ck = xcb_intern_atom(conn, false, len, name);
  xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(conn, ck, nullptr);
  if (!r) {
    return XCB_NONE;
  }
  const xcb_atom_t a = r->atom;
  std::free(r);
  return a;
}

QString readUtf8Property(xcb_connection_t *conn, xcb_window_t w, xcb_atom_t prop, xcb_atom_t typ) {
  if (prop == XCB_NONE || typ == XCB_NONE || w == XCB_NONE) {
    return {};
  }
  xcb_get_property_cookie_t ck = xcb_get_property(conn, false, w, prop, typ, 0, 2048);
  xcb_get_property_reply_t *r = xcb_get_property_reply(conn, ck, nullptr);
  if (!r || xcb_get_property_value_length(r) <= 0) {
    std::free(r);
    return {};
  }
  const int n = xcb_get_property_value_length(r);
  QString out = QString::fromUtf8(static_cast<const char *>(xcb_get_property_value(r)), n);
  std::free(r);
  return out;
}

QString readStringProperty(xcb_connection_t *conn, xcb_window_t w, xcb_atom_t prop, xcb_atom_t typ) {
  if (prop == XCB_NONE || typ == XCB_NONE || w == XCB_NONE) {
    return {};
  }
  xcb_get_property_cookie_t ck = xcb_get_property(conn, false, w, prop, typ, 0, 1024);
  xcb_get_property_reply_t *r = xcb_get_property_reply(conn, ck, nullptr);
  if (!r || xcb_get_property_value_length(r) <= 0) {
    std::free(r);
    return {};
  }
  const int n = xcb_get_property_value_length(r);
  QString out = QString::fromLatin1(static_cast<const char *>(xcb_get_property_value(r)), n);
  std::free(r);
  return out;
}

void logX11WindowManagerInfoImpl(QGuiApplication *app) {
  if (!app) {
    return;
  }
  auto *x11 = app->nativeInterface<QNativeInterface::QX11Application>();
  if (!x11) {
    qInfo().noquote() << "[Client][X11WM] QNativeInterface::QX11Application=null — 跳过";
    return;
  }
  xcb_connection_t *conn = x11->connection();
  if (!conn || xcb_connection_has_error(conn) != 0) {
    qWarning().noquote() << "[Client][X11WM] xcb 连接无效或已有错误 — 跳过";
    return;
  }

  const xcb_setup_t *setup = xcb_get_setup(conn);
  if (!setup) {
    qWarning().noquote() << "[Client][X11WM] xcb_get_setup 失败 — 跳过";
    return;
  }
  xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
  if (!it.rem) {
    qWarning().noquote() << "[Client][X11WM] 无 root screen — 跳过";
    return;
  }
  const xcb_window_t root = it.data->root;

  const xcb_atom_t aNetCheck = internAtomName(conn, "_NET_SUPPORTING_WM_CHECK");
  if (aNetCheck == XCB_NONE) {
    qWarning().noquote() << "[Client][X11WM] internAtom(_NET_SUPPORTING_WM_CHECK) 失败";
    return;
  }

  xcb_get_property_cookie_t ckRoot =
      xcb_get_property(conn, false, root, aNetCheck, XCB_GET_PROPERTY_TYPE_ANY, 0, 1);
  xcb_get_property_reply_t *prCheck = xcb_get_property_reply(conn, ckRoot, nullptr);
  if (!prCheck || prCheck->format != 32 || xcb_get_property_value_length(prCheck) < static_cast<int>(sizeof(xcb_window_t))) {
    qInfo().nospace().noquote()
        << "[Client][X11WM] root 上无有效 _NET_SUPPORTING_WM_CHECK（format="
        << (prCheck ? int(prCheck->format) : -1) << " len="
        << (prCheck ? xcb_get_property_value_length(prCheck) : -1)
        << "）— 可能无 EWMH 或非标准会话";
    std::free(prCheck);
    return;
  }
  const xcb_window_t wmChild =
      *reinterpret_cast<const xcb_window_t *>(xcb_get_property_value(prCheck));
  std::free(prCheck);

  if (wmChild == XCB_NONE) {
    qInfo().noquote() << "[Client][X11WM] _NET_SUPPORTING_WM_CHECK 指向窗口 0 — 跳过命名解析";
    return;
  }

  const xcb_atom_t aNetWmName = internAtomName(conn, "_NET_WM_NAME");
  const xcb_atom_t aUtf8String = internAtomName(conn, "UTF8_STRING");
  QString wmName = readUtf8Property(conn, wmChild, aNetWmName, aUtf8String);
  if (wmName.isEmpty()) {
    const xcb_atom_t aWmName = internAtomName(conn, "WM_NAME");
    const xcb_atom_t aString = internAtomName(conn, "STRING");
    wmName = readStringProperty(conn, wmChild, aWmName, aString);
  }

  QString cmHint;
  const xcb_atom_t aCmS0 = internAtomName(conn, "_NET_WM_CM_S0");
  if (aCmS0 != XCB_NONE) {
    xcb_get_property_cookie_t ckCm =
        xcb_get_property(conn, false, root, aCmS0, XCB_GET_PROPERTY_TYPE_ANY, 0, 1);
    xcb_get_property_reply_t *prCm = xcb_get_property_reply(conn, ckCm, nullptr);
    if (prCm && prCm->format == 32 && xcb_get_property_value_length(prCm) >= static_cast<int>(sizeof(xcb_window_t))) {
      const xcb_window_t cmWin =
          *reinterpret_cast<const xcb_window_t *>(xcb_get_property_value(prCm));
      cmHint = QStringLiteral(" netWmCmS0Xid=0x%1")
                   .arg(QString::number(static_cast<quintptr>(cmWin), 16));
    }
    std::free(prCm);
  }

  if (!wmName.isEmpty())
    ClientVideoDiagCache::setX11NetWmName(wmName);

  qInfo().nospace().noquote()
      << "[Client][X11WM] EWMH wmCheckXid=0x" << QString::number(static_cast<quintptr>(wmChild), 16)
      << " _NET_WM_NAME/WM_NAME=\"" << wmName << "\"" << cmHint
      << " root=0x" << QString::number(static_cast<quintptr>(root), 16)
      << " | 透底/合成问题对照混成器实现与 [Client][CompositingRoot][5Why]";
}

void runVisualProbeImpl() {
  auto *app = qobject_cast<QGuiApplication *>(QGuiApplication::instance());
  if (!app || shouldSkipPlatform(app)) {
    return;
  }
  auto *x11 = app->nativeInterface<QNativeInterface::QX11Application>();
  if (!x11) {
    qInfo().noquote() << "[Client][X11Visual] QNativeInterface::QX11Application=null — 跳过";
    return;
  }
  xcb_connection_t *conn = x11->connection();
  if (!conn || xcb_connection_has_error(conn) != 0) {
    qWarning().noquote() << "[Client][X11Visual] xcb 连接无效或已有错误 — 跳过";
    return;
  }

  const auto tops = QGuiApplication::topLevelWindows();
  int n = 0;
  for (QWindow *w : tops) {
    if (auto *qw = qobject_cast<QQuickWindow *>(w)) {
      logOneQuickWindow(qw, conn);
      ++n;
    }
  }
  if (n == 0) {
    qInfo().noquote() << "[Client][X11Visual] 无顶层 QQuickWindow，跳过";
  }
}

#endif  // CLIENT_HAVE_XCB

}  // namespace

namespace ClientX11VisualProbe {

void scheduleLogTopLevelQuickWindowVisuals() {
  if (visualProbeDisabled()) {
    qInfo().noquote() << "[Client][X11Visual] CLIENT_X11_VISUAL_PROBE=0 — 已禁用自动 visual 日志";
    return;
  }

  auto *app = qGuiApp;
  if (!app || shouldSkipPlatform(qobject_cast<QGuiApplication *>(app))) {
    return;
  }

#if !defined(CLIENT_HAVE_XCB)
  qInfo().noquote() << "[Client][X11Visual] 未编译 CLIENT_HAVE_XCB — 无 xcb visual 探测（可安装 "
                       "libxcb 并重建客户端）";
  return;
#else
  QTimer::singleShot(0, app, []() {
    runVisualProbeImpl();
    // winId 在首帧前可能仍为 0：延迟再采一次（仅多一行日志，便于钉根因）
    QTimer::singleShot(350, qGuiApp, []() { runVisualProbeImpl(); });
  });
#endif
}

void logX11WindowManagerInfo() {
#if !defined(CLIENT_HAVE_XCB)
  qInfo().noquote() << "[Client][X11WM] 未编译 CLIENT_HAVE_XCB — 无 EWMH WM 探测（可安装 libxcb 并重建客户端）";
  return;
#else
  auto *app = qobject_cast<QGuiApplication *>(QGuiApplication::instance());
  if (!app || shouldSkipPlatform(app)) {
    return;
  }
  if (wmProbeDisabled()) {
    qInfo().noquote() << "[Client][X11WM] CLIENT_X11_WM_PROBE=0 — 已禁用 WM/混成器标识日志";
    return;
  }
  logX11WindowManagerInfoImpl(app);
#endif
}

}  // namespace ClientX11VisualProbe

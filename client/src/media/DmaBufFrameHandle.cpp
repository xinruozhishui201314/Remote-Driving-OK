#include "DmaBufFrameHandle.h"

#include <QCoreApplication>

static void registerSharedDmaBufFrame() {
  qRegisterMetaType<SharedDmaBufFrame>("SharedDmaBufFrame");
}

Q_COREAPP_STARTUP_FUNCTION(registerSharedDmaBufFrame)

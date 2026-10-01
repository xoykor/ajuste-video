// SPDX-License-Identifier: GPL-3.0-or-later
#include "effect.h"

namespace KWin
{
KWIN_EFFECT_FACTORY_SUPPORTED(AjusteVideoEffect, "metadata.json", return AjusteVideoEffect::supported();)
}

#include "effectplugin.moc"

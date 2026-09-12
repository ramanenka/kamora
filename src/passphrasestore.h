#pragma once

#include <QString>

/**
 * Keeps the borg passphrase in KWallet, falling back to a 0600 file when no
 * wallet is available (for example on a headless autostart before login).
 */
namespace PassphraseStore
{
/// Stores the passphrase; returns false when nothing could be written.
bool store(const QString &repoId, const QString &passphrase);

/// Reads the passphrase back, empty when there is none.
QString lookup(const QString &repoId);

void remove(const QString &repoId);
}

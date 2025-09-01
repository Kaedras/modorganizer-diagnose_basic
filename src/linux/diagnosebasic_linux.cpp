#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>

bool checkFileAttributes(const QString& path)
{
  // check if e2fsprogs is installed
  QString lsattr = QStandardPaths::findExecutable(QStringLiteral("lsattr"));
  if (lsattr.isEmpty()) {
    qWarning() << "lsattr not found, check if e2fsprogs is installed";
    return false;
  }

  QProcess p;
  p.setProgram(lsattr);
  p.setArguments({path});
  p.start();
  if (!p.waitForFinished(1000)) {
    qWarning() << qUtf8Printable(
        QStringLiteral("Unable to get file attributes for %1 (%2)")
            .arg(path, p.errorString()));
    return false;
  }
  if (p.exitCode() != 0) {
    qWarning() << qUtf8Printable(
        QStringLiteral("Unable to get file attributes for %1 (%2)")
            .arg(path, p.readAllStandardError()));
    return false;
  }
  QString result = p.readAllStandardOutput();

  qDebug() << result;

  return true;
}

bool fixFileAttributes(const QString& path)
{
  // I'm not sure if file attributes can cause issues on linux
  (void)path;
  return true;
}

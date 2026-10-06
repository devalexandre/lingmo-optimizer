#include "suggestions.h"

#include "cleaner.h"
#include "core/diskusage.h"
#include "core/format.h"
#include "core/hogs.h"
#include "core/policy.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtConcurrent>

#include <algorithm>
#include <functional>

#include <sys/stat.h>
#include <unistd.h>

static QString tr_(const char *text)
{
    return QCoreApplication::translate("Suggestions", text);
}

static constexpr qint64 GiB = 1024LL * 1024 * 1024;
static constexpr qint64 MiB = 1024LL * 1024;

namespace {

DuNode *findPath(DuNode *root, const QString &rootPath, const QString &path)
{
    if (path == rootPath)
        return root;
    if (!path.startsWith(rootPath + QLatin1Char('/')))
        return nullptr;
    DuNode *node = root;
    for (const QString &part : path.mid(rootPath.size() + 1).split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        node = node ? node->child(part) : nullptr;
    }
    return node;
}

QVariantMap child(const QString &name, const QString &path, qint64 size, const QString &detail,
                  const QStringList &trashPaths = {}, const QString &ollamaName = QString())
{
    bool canTrash = !trashPaths.isEmpty();
    for (const QString &p : trashPaths)
        canTrash = canTrash && Policy::mayMoveToTrash(p, QDir::homePath(), ::getuid());
    return QVariantMap {{QStringLiteral("name"), name}, {QStringLiteral("path"), path},
                        {QStringLiteral("size"), double(size)}, {QStringLiteral("detail"), detail},
                        {QStringLiteral("trashPaths"), trashPaths}, {QStringLiteral("canTrash"), canTrash},
                        {QStringLiteral("ollamaName"), ollamaName}};
}

QVariantMap item(const QString &id, const QString &title, const QString &description, qint64 size,
                 const QString &path, const QString &icon, const QString &action, const QVariantList &children = {},
                 const QString &command = QString())
{
    return QVariantMap {{QStringLiteral("id"), id}, {QStringLiteral("title"), title},
                        {QStringLiteral("description"), description}, {QStringLiteral("size"), double(size)},
                        {QStringLiteral("path"), path}, {QStringLiteral("icon"), icon},
                        {QStringLiteral("action"), action}, {QStringLiteral("children"), children},
                        {QStringLiteral("command"), command}};
}

QString ageText(qint64 seconds)
{
    const qint64 days = seconds / 86400;
    if (days < 1)
        return tr_("modificado hoje");
    if (days == 1)
        return tr_("modificado há 1 dia");
    if (days < 60)
        return tr_("modificado há %1 dias").arg(days);
    return tr_("modificado há %1 meses").arg(days / 30);
}

struct OllamaModel {
    QString name;
    qint64 size = 0;
};

QVector<OllamaModel> ollamaFromManifests(const QString &modelsDir, bool *readable)
{
    QVector<OllamaModel> models;
    const QString manifests = modelsDir + QStringLiteral("/manifests");
    *readable = QFileInfo(manifests).isReadable() && QFileInfo(manifests).isDir();
    if (!*readable)
        return models;
    // manifests/<registry>/<namespace>/<model>/<tag>
    QDirIterator it(manifests, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QStringList parts = file.mid(manifests.size() + 1).split(QLatin1Char('/'));
        if (parts.size() != 4)
            continue;
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject json = QJsonDocument::fromJson(f.readAll()).object();
        qint64 size = json.value(QStringLiteral("config")).toObject().value(QStringLiteral("size")).toInteger();
        for (const QJsonValue &layer : json.value(QStringLiteral("layers")).toArray())
            size += layer.toObject().value(QStringLiteral("size")).toInteger();
        QString name = parts.at(2) + QLatin1Char(':') + parts.at(3);
        if (parts.at(1) != QLatin1String("library"))
            name = parts.at(1) + QLatin1Char('/') + name;
        if (parts.at(0) != QLatin1String("registry.ollama.ai"))
            name = parts.at(0) + QLatin1Char('/') + name;
        models.append({name, size});
    }
    return models;
}

QVector<OllamaModel> ollamaFromCli()
{
    QVector<OllamaModel> models;
    const QString ollama = QStandardPaths::findExecutable(QStringLiteral("ollama"));
    if (ollama.isEmpty())
        return models;
    QProcess p;
    p.start(ollama, {QStringLiteral("list")});
    if (!p.waitForFinished(5000) || p.exitCode() != 0) {
        p.kill();
        return models;
    }
    // NAME  ID  SIZE  MODIFIED
    const QStringList lines = QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    static const QRegularExpression re(QStringLiteral("^(\\S+)\\s+\\S+\\s+([0-9.]+ [KMGT]?B)\\s"));
    for (const QString &line : lines.mid(1)) {
        const auto m = re.match(line);
        if (m.hasMatch())
            models.append({m.captured(1), Hogs::parseHumanSize(m.captured(2))});
    }
    return models;
}

QVariantList collect(std::shared_ptr<ScanProgress> progress)
{
    const QString home = QDir::homePath();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QVariantList items;

    // ---- one walk of the home folder for everything inside it
    DiskScanner scanner;
    auto homeTree = scanner.scan(home, progress.get());
    if (progress->cancel.load())
        return {};
    DuNode *root = homeTree.get();

    auto nodeSize = [&](const QString &path) -> qint64 {
        const DuNode *n = findPath(root, home, path);
        return n ? n->size : 0;
    };

    // ~/.cache
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (const qint64 size = nodeSize(cacheDir); size > 0) {
        qint64 safe = 0;
        for (const QString &dir : Cleaner::safeCacheDirs())
            safe += nodeSize(dir) > 0 ? nodeSize(dir) : DiskScanner::totalSize(dir);
        QVariantList children;
        if (DuNode *n = findPath(root, home, cacheDir)) {
            int count = 0;
            for (const auto &c : n->children) {
                if (c->kind == DuNode::Others || count >= 8)
                    continue;
                children.append(child(c->displayName(), c->path(), c->size, QString(), {c->path()}));
                ++count;
            }
        }
        items.append(item(QStringLiteral("user-cache"), tr_("Cache de programas (~/.cache)"),
                          tr_("Arquivos temporários que os programas guardam para abrir mais rápido. \"Limpar\" apaga só os "
                              "caches seguros (%1: miniaturas, pip, npm, yarn, Go, shaders). Os de navegadores e "
                              "modelos de IA ficam; apague-os pela lista abaixo se quiser.")
                              .arg(Format::bytes(safe)),
                          size, cacheDir, QStringLiteral("folder-temp"), QStringLiteral("user-cache"), children));
    }

    // Trash
    const QString trash = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash");
    if (const qint64 size = nodeSize(trash); size > 64 * 1024) {
        items.append(item(QStringLiteral("trash"), tr_("Lixeira"),
                          tr_("Arquivos que você apagou continuam ocupando espaço até a lixeira ser esvaziada."),
                          size, trash + QStringLiteral("/files"), QStringLiteral("user-trash-full"), QStringLiteral("empty-trash")));
    }

    // Downloads
    const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (downloads != home) {
        if (DuNode *n = findPath(root, home, downloads); n && n->size > 0) {
            QVariantList children;
            int count = 0;
            for (const auto &c : n->children) {
                if (c->kind == DuNode::Others || count >= 10)
                    continue;
                children.append(child(c->displayName(), c->path(), c->size, QString(), {c->path()}));
                ++count;
            }
            items.append(item(QStringLiteral("downloads"), tr_("Downloads"),
                              tr_("Instaladores, ISOs e arquivos baixados que talvez você não precise mais."),
                              n->size, downloads, QStringLiteral("folder-downloads"), QStringLiteral("open"), children));
        }
    }

    // Walk the tree once for big files, disk images and build folders
    QVector<const DuNode *> bigFiles, images;
    QHash<QString, QPair<qint64, QStringList>> projects; // project -> (size, build dirs)
    QStringList projectRoots;
    for (const char *name : {"projects", "Projects", "projetos", "Projetos", "dev", "src", "code", "git", "workspace", "repos"}) {
        const QString dir = home + QLatin1Char('/') + QLatin1String(name);
        if (QFileInfo(dir).isDir())
            projectRoots.append(dir);
    }
    std::function<void(const DuNode *, const QString &, const QString &)> walk =
        [&](const DuNode *node, const QString &path, const QString &projectRoot) {
        for (const auto &c : node->children) {
            if (c->kind == DuNode::File) {
                const QString lower = QString::fromLocal8Bit(c->name).toLower();
                if (c->size >= GiB)
                    bigFiles.append(c.get());
                if (c->size >= 50 * MiB && (lower.endsWith(QLatin1String(".iso")) || lower.endsWith(QLatin1String(".img"))))
                    images.append(c.get());
            } else if (c->kind == DuNode::Dir) {
                const QString childPath = path + QLatin1Char('/') + QFile::decodeName(c->name);
                QString inProjects = projectRoot;
                if (inProjects.isEmpty() && projectRoots.contains(childPath))
                    inProjects = childPath;
                if (!inProjects.isEmpty() && c->size >= MiB && Hogs::isBuildDir(childPath)) {
                    // Project = nearest folder with .git, else the folder holding it
                    QString project = QFileInfo(childPath).absolutePath();
                    for (QString p = project; p.size() > inProjects.size(); p = QFileInfo(p).absolutePath()) {
                        if (QFileInfo::exists(p + QStringLiteral("/.git"))) {
                            project = p;
                            break;
                        }
                    }
                    auto &entry = projects[project];
                    entry.first += c->size;
                    entry.second.append(childPath);
                    continue; // nothing interesting inside node_modules
                }
                walk(c.get(), childPath, inProjects);
            }
        }
    };
    walk(root, home, QString());

    auto bySize = [](const DuNode *a, const DuNode *b) { return a->size > b->size; };
    if (!bigFiles.isEmpty()) {
        std::sort(bigFiles.begin(), bigFiles.end(), bySize);
        QVariantList children;
        qint64 total = 0;
        for (int i = 0; i < bigFiles.size() && i < 20; ++i) {
            const QString path = bigFiles.at(i)->path();
            children.append(child(QFileInfo(path).fileName(), path, bigFiles.at(i)->size,
                                  QDir(home).relativeFilePath(QFileInfo(path).absolutePath()), {path}));
            total += bigFiles.at(i)->size;
        }
        items.append(item(QStringLiteral("big-files"), tr_("Arquivos grandes na pasta pessoal"),
                          tr_("Arquivos com mais de 1 GB: vídeos, máquinas virtuais, backups, imagens de disco..."),
                          total, home, QStringLiteral("document-multiple"), QStringLiteral("none"), children));
    }
    if (!images.isEmpty()) {
        std::sort(images.begin(), images.end(), bySize);
        QVariantList children;
        qint64 total = 0;
        for (int i = 0; i < images.size() && i < 20; ++i) {
            const QString path = images.at(i)->path();
            struct stat st;
            QString detail = QDir(home).relativeFilePath(QFileInfo(path).absolutePath());
            if (::lstat(QFile::encodeName(path).constData(), &st) == 0)
                detail += QStringLiteral(" · ") + ageText(now - st.st_mtime);
            children.append(child(QFileInfo(path).fileName(), path, images.at(i)->size, detail, {path}));
            total += images.at(i)->size;
        }
        items.append(item(QStringLiteral("images"), tr_("Imagens de disco (ISO/IMG)"),
                          tr_("Depois de instalar o sistema ou gravar o pendrive, a imagem baixada não é mais necessária."),
                          total, home, QStringLiteral("media-optical"), QStringLiteral("none"), children));
    }
    if (!projects.isEmpty()) {
        QVector<QString> keys = projects.keys().toVector();
        std::sort(keys.begin(), keys.end(), [&projects](const QString &a, const QString &b) {
            return projects.value(a).first > projects.value(b).first;
        });
        QVariantList children;
        qint64 total = 0;
        for (const QString &key : std::as_const(keys))
            total += projects.value(key).first;
        for (int i = 0; i < keys.size() && i < 20; ++i) {
            const auto &entry = projects.value(keys.at(i));
            QStringList names;
            for (const QString &dir : entry.second)
                names.append(QDir(keys.at(i)).relativeFilePath(dir));
            names.removeDuplicates();
            QString detail = names.mid(0, 4).join(QStringLiteral(", "));
            if (names.size() > 4)
                detail += tr_(" e mais %1").arg(names.size() - 4);
            children.append(child(QDir(home).relativeFilePath(keys.at(i)), keys.at(i), entry.first, detail, entry.second));
        }
        items.append(item(QStringLiteral("build-dirs"), tr_("Dependências e builds de projetos"),
                          tr_("Pastas node_modules, target, build, .venv... São recriadas pelo npm/cargo/cmake na próxima "
                              "compilação. Projetos parados há tempo são bons candidatos."),
                          total, projectRoots.value(0), QStringLiteral("folder-development"), QStringLiteral("none"), children));
    }

    // Podman (rootless) storage
    const QString podman = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/containers/storage");
    if (const qint64 size = nodeSize(podman); size > 0) {
        items.append(item(QStringLiteral("podman"), tr_("Contêineres Podman"),
                          tr_("Imagens e contêineres do Podman. Para liberar o que não está em uso, rode no terminal:"),
                          size, podman, QStringLiteral("application-x-executable"), QStringLiteral("none"), {},
                          QStringLiteral("podman system prune")));
    }

    // Ollama models
    {
        QVariantList children;
        qint64 total = 0;
        bool anyReadable = false;
        QStringList dirs = {home + QStringLiteral("/.ollama/models"), QStringLiteral("/usr/share/ollama/.ollama/models"),
                            QStringLiteral("/var/lib/ollama/.ollama/models")};
        QSet<QString> names;
        QString shownPath;
        for (const QString &dir : dirs) {
            bool readable = false;
            const QVector<OllamaModel> models = ollamaFromManifests(dir, &readable);
            if (!readable)
                continue;
            anyReadable = true;
            if (models.isEmpty())
                continue;
            if (shownPath.isEmpty())
                shownPath = dir;
            total += dir.startsWith(home) ? nodeSize(dir) : DiskScanner::totalSize(dir);
            for (const OllamaModel &m : models) {
                if (names.contains(m.name))
                    continue;
                names.insert(m.name);
                children.append(child(m.name, dir, m.size, QString(), {}, m.name));
            }
        }
        // The service's own folder is often unreadable: ask the server
        const QVector<OllamaModel> listed = ollamaFromCli();
        for (const OllamaModel &m : listed) {
            if (names.contains(m.name))
                continue;
            names.insert(m.name);
            children.append(child(m.name, QString(), m.size, QString(), {}, m.name));
            total += m.size;
        }
        Q_UNUSED(anyReadable);
        if (!children.isEmpty()) {
            std::sort(children.begin(), children.end(), [](const QVariant &a, const QVariant &b) {
                return a.toMap().value(QStringLiteral("size")).toDouble() > b.toMap().value(QStringLiteral("size")).toDouble();
            });
            items.append(item(QStringLiteral("ollama"), tr_("Modelos do Ollama"),
                              tr_("Modelos de IA baixados pelo Ollama. Remova os que não usa mais (ollama rm); "
                                  "modelos que compartilham arquivos liberam menos que o tamanho mostrado."),
                              total, shownPath, QStringLiteral("applications-science"), QStringLiteral("none"), children));
        }
    }
    if (progress->cancel.load())
        return {};

    // ---- outside home (read-only looks)
    const qint64 pacman = DiskScanner::totalSize(QStringLiteral("/var/cache/pacman/pkg"));
    if (pacman > 0) {
        items.append(item(QStringLiteral("pacman-cache"), tr_("Cache de pacotes do pacman"),
                          tr_("Cada atualização guarda o pacote baixado em /var/cache/pacman/pkg. \"Limpar\" mantém só a "
                              "versão atual de cada programa instalado e apaga os pacotes desinstalados."),
                          pacman, QStringLiteral("/var/cache/pacman/pkg"), QStringLiteral("package-x-generic"),
                          QStringLiteral("pacman-cache")));
    }
    const qint64 journal = Cleaner::journalSize();
    if (journal > 0) {
        items.append(item(QStringLiteral("journal"), tr_("Registros do sistema (journal)"),
                          tr_("Mensagens do systemd usadas para investigar problemas. \"Limpar\" mantém só os últimos 7 dias."),
                          journal, QStringLiteral("/var/log/journal"), QStringLiteral("text-x-log"), QStringLiteral("journal")));
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("flatpak")).isEmpty()) {
        const qint64 system = DiskScanner::totalSize(QStringLiteral("/var/lib/flatpak"));
        const qint64 user = nodeSize(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/flatpak"));
        if (system + user > 0) {
            items.append(item(QStringLiteral("flatpak"), tr_("Flatpak"),
                              tr_("Aplicativos Flatpak e os runtimes que eles usam. \"Limpar\" remove só os runtimes que nenhum "
                                  "aplicativo instalado usa mais (flatpak uninstall --unused)."),
                              system + user, QStringLiteral("/var/lib/flatpak"), QStringLiteral("package-x-generic"),
                              QStringLiteral("flatpak")));
        }
    }
    // Docker: /var/lib/docker is root-only; ask the daemon when we may
    const QString docker = QStandardPaths::findExecutable(QStringLiteral("docker"));
    if (!docker.isEmpty() && QFileInfo(QStringLiteral("/var/run/docker.sock")).isWritable()) {
        QProcess p;
        p.start(docker, {QStringLiteral("system"), QStringLiteral("df"), QStringLiteral("--format"),
                         QStringLiteral("{{.Type}}\t{{.Size}}\t{{.Reclaimable}}")});
        if (p.waitForFinished(10000) && p.exitCode() == 0) {
            QVariantList children;
            qint64 total = 0;
            for (const QString &line : QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                const QStringList cols = line.split(QLatin1Char('\t'));
                if (cols.size() < 3)
                    continue;
                const qint64 size = Hogs::parseHumanSize(cols.at(1));
                if (size <= 0)
                    continue;
                total += size;
                static const QHash<QString, const char *> names = {
                    {QStringLiteral("Images"), QT_TRANSLATE_NOOP("Suggestions", "Imagens")},
                    {QStringLiteral("Containers"), QT_TRANSLATE_NOOP("Suggestions", "Contêineres")},
                    {QStringLiteral("Local Volumes"), QT_TRANSLATE_NOOP("Suggestions", "Volumes locais")},
                    {QStringLiteral("Build Cache"), QT_TRANSLATE_NOOP("Suggestions", "Cache de build")},
                };
                const QString name = names.contains(cols.at(0)) ? tr_(names.value(cols.at(0))) : cols.at(0);
                const qint64 reclaimable = Hogs::parseHumanSize(cols.at(2).section(QLatin1Char(' '), 0, 0));
                children.append(child(name, QString(), size,
                                      reclaimable >= 0 ? tr_("%1 não estão em uso").arg(Format::bytes(reclaimable)) : QString()));
            }
            if (total > 0) {
                items.append(item(QStringLiteral("docker"), tr_("Docker"),
                                  tr_("Imagens, contêineres, volumes e cache de build do Docker. Para apagar o que não está em uso, "
                                      "rode no terminal (confira antes: volumes podem ter dados):"),
                                  total, QString(), QStringLiteral("application-x-executable"), QStringLiteral("none"),
                                  children, QStringLiteral("docker system prune")));
            }
        } else {
            p.kill();
        }
    }

    std::sort(items.begin(), items.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("size")).toDouble() > b.toMap().value(QStringLiteral("size")).toDouble();
    });
    return items;
}

} // namespace

Suggestions::Suggestions(QObject *parent)
    : QObject(parent)
{
}

Suggestions::~Suggestions()
{
    if (m_progress)
        m_progress->cancel = true;
    m_future.waitForFinished();
}

void Suggestions::refresh()
{
    if (m_busy)
        return;
    m_busy = true;
    emit busyChanged();
    m_progress = std::make_shared<ScanProgress>();
    auto *watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher] {
        m_items = watcher->result();
        watcher->deleteLater();
        m_busy = false;
        m_loaded = true;
        emit itemsChanged();
        emit busyChanged();
    });
    m_future = QtConcurrent::run(collect, m_progress);
    watcher->setFuture(m_future);
}

void Suggestions::removeOllamaModel(const QString &name)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9._:/-]+$"));
    const QString ollama = QStandardPaths::findExecutable(QStringLiteral("ollama"));
    if (ollama.isEmpty() || !valid.match(name).hasMatch() || name.startsWith(QLatin1Char('-'))) {
        emit ollamaRemoved(name, false, tr("O comando ollama não está disponível"));
        return;
    }
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this, [this, process, name](int code, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && code == 0;
        const QString output = QString::fromUtf8(process->readAll()).trimmed();
        process->deleteLater();
        emit ollamaRemoved(name, ok, ok ? QString() : (output.isEmpty() ? tr("O servidor do Ollama não respondeu") : output));
        refresh();
    });
    process->start(ollama, {QStringLiteral("rm"), name});
}

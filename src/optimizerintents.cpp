#include "optimizerintents.h"

#include "core/devcaches.h"
#include "core/format.h"

#include <LingmoIntents/intentservice.h>

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QtConcurrent>

#include <atomic>

using LingmoIntents::IntentCall;
using LingmoIntents::IntentResult;
using namespace DevCaches;

namespace OptimizerIntents {

bool requested(int argc, char *argv[])
{
    return argc == 2 && qstrcmp(argv[1], "--intents") == 0;
}

static std::atomic<int> s_busy {0};

// Runs `work` in a thread and replies with its result
static void inBackground(const IntentCall &call, std::function<IntentResult()> work)
{
    ++s_busy;
    auto *watcher = new QFutureWatcher<IntentResult>(qApp);
    QObject::connect(watcher, &QFutureWatcher<IntentResult>::finished, qApp, [watcher, call] {
        call.finish(watcher->result());
        watcher->deleteLater();
        --s_busy;
    });
    watcher->setFuture(QtConcurrent::run(std::move(work)));
}

int run(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("lingmoos"));
    QCoreApplication::setApplicationName(QStringLiteral("lingmo-optimizer"));

    LingmoIntents::IntentService intents(QStringLiteral("lingmo-optimizer"));

    intents.handleAsync(QStringLiteral("optimizer.analyze"), [](const IntentCall &call) {
        const bool projects = call.params().value(QStringLiteral("projects")).toBool();
        inBackground(call, [projects] {
            Env env = Env::system();
            env.scanProjects = projects;
            QList<Item> items = detect(env);
            measure(items, env, nullptr, 15000, 90000);
            qint64 total = 0, reclaimable = 0;
            QStringList lines;
            for (const QString &group : groupOrder()) {
                qint64 size = 0, safe = 0;
                for (const Item &item : std::as_const(items)) {
                    if (item.group != group || item.size <= 0)
                        continue;
                    size += item.size;
                    if (safeForAutomation(item))
                        safe += item.size;
                }
                if (size <= 0)
                    continue;
                total += size;
                reclaimable += safe;
                lines.append(QCoreApplication::translate("OptimizerIntents", "%1: %2 (%3 podem ser limpos com segurança)")
                                 .arg(groupTitle(group), Format::bytes(size), Format::bytes(safe)));
            }
            return IntentResult::ok({{QStringLiteral("total"), Format::bytes(total)},
                                     {QStringLiteral("reclaimable"), Format::bytes(reclaimable)},
                                     {QStringLiteral("totalBytes"), double(total)},
                                     {QStringLiteral("reclaimableBytes"), double(reclaimable)},
                                     {QStringLiteral("summary"), lines.join(QLatin1Char('\n'))}});
        });
    });

    // write: the gate already demanded context.confirmed = true
    intents.handleAsync(QStringLiteral("optimizer.clean.caches"), [](const IntentCall &call) {
        const QStringList groups = call.params().value(QStringLiteral("groups")).toStringList();
        inBackground(call, [groups] {
            Env env = Env::system();
            env.scanProjects = false;   // projects are never cleaned from here
            QList<Item> items = detect(env);
            items.erase(std::remove_if(items.begin(), items.end(), [&groups](const Item &item) {
                            return !safeForAutomation(item) || (!groups.isEmpty() && !groups.contains(item.group));
                        }),
                        items.end());
            measure(items, env, nullptr, 15000, 90000);
            std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.order < b.order; });
            qint64 freed = 0;
            int cleaned = 0;
            QStringList lines;
            for (const Item &item : std::as_const(items)) {
                const Result r = clean(item, env);
                if (r.ok) {
                    ++cleaned;
                    freed += r.freed;
                    lines.append(QStringLiteral("%1: %2").arg(item.title, Format::bytes(r.freed)));
                } else {
                    lines.append(QStringLiteral("%1: %2").arg(item.title, r.message));
                }
            }
            if (items.isEmpty())
                lines.append(QCoreApplication::translate("OptimizerIntents", "Nenhum cache para limpar."));
            return IntentResult::ok({{QStringLiteral("freed"), Format::bytes(freed)},
                                     {QStringLiteral("freedBytes"), double(freed)},
                                     {QStringLiteral("cleaned"), cleaned},
                                     {QStringLiteral("summary"), lines.join(QLatin1Char('\n'))}});
        });
    });

    if (!intents.publish(LingmoIntents::IntentService::Headless))
        return 1;
    intents.setIdleQuit(30000, [] { return s_busy.load() == 0; });
    return app.exec();
}

}

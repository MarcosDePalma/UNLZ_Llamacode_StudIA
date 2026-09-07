#include "AuxiliaryJobScheduler.h"

#include <QtGlobal>
#include <QUuid>
#include <QVariantMap>

AuxiliaryJobScheduler::AuxiliaryJobScheduler(QObject *parent)
    : QObject(parent)
{
    m_classLimits.insert(QStringLiteral("interactive_text"), 1);
    m_classLimits.insert(QStringLiteral("agent_tool"), 2);
    m_classLimits.insert(QStringLiteral("voice"), 1);
    m_classLimits.insert(QStringLiteral("document"), 1);
    m_classLimits.insert(QStringLiteral("retrieval"), 1);
    m_classLimits.insert(QStringLiteral("verification"), 1);
    m_classLimits.insert(QStringLiteral("benchmark"), 1);
    m_classLimits.insert(QStringLiteral("background_maintenance"), 1);
}

void AuxiliaryJobScheduler::setClassLimit(const QString &jobClass, int limit)
{
    const int normalized = qMax(0, limit);
    if (m_classLimits.value(jobClass, 1) == normalized) return;
    m_classLimits.insert(jobClass, normalized);
    emit jobsChanged();
}

int AuxiliaryJobScheduler::classLimit(const QString &jobClass) const
{
    return m_classLimits.value(jobClass, 1);
}

QString AuxiliaryJobScheduler::enqueue(const QString &jobClass, const QString &resourceKey,
                                       int priority, const QString &detail)
{
    Job job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.jobClass = jobClass;
    job.resourceKey = resourceKey;
    job.priority = priority;
    job.detail = detail;
    job.sequence = m_nextSequence++;
    m_jobs.append(job);
    emit jobQueued(job.id);
    emit jobsChanged();
    return job.id;
}

bool AuxiliaryJobScheduler::startNext(QString *startedId)
{
    const int index = bestQueuedIndex();
    if (index < 0) return false;
    m_jobs[index].state = JobState::Running;
    if (startedId) *startedId = m_jobs[index].id;
    emit jobStarted(m_jobs[index].id);
    emit jobsChanged();
    return true;
}

QString AuxiliaryJobScheduler::startNextJob()
{
    QString startedId;
    if (!startNext(&startedId)) return {};
    return startedId;
}

bool AuxiliaryJobScheduler::complete(const QString &id, bool ok, const QString &detail)
{
    Job *job = findJob(id);
    if (!job || job->state != JobState::Running) return false;
    job->state = ok ? JobState::Completed : JobState::Failed;
    job->detail = detail;
    emit jobFinished(id, ok);
    emit jobsChanged();
    return true;
}

bool AuxiliaryJobScheduler::cancel(const QString &id, const QString &detail)
{
    Job *job = findJob(id);
    if (!job || job->state == JobState::Completed || job->state == JobState::Failed
        || job->state == JobState::Cancelled) {
        return false;
    }
    job->state = JobState::Cancelled;
    job->detail = detail;
    emit jobCancelled(id);
    emit jobsChanged();
    return true;
}

QVariantList AuxiliaryJobScheduler::snapshot() const
{
    QVariantList rows;
    for (const Job &job : m_jobs) {
        rows << QVariantMap{
            {QStringLiteral("id"), job.id},
            {QStringLiteral("class"), job.jobClass},
            {QStringLiteral("resourceKey"), job.resourceKey},
            {QStringLiteral("priority"), job.priority},
            {QStringLiteral("state"), stateName(job.state)},
            {QStringLiteral("detail"), job.detail},
            {QStringLiteral("sequence"), QVariant::fromValue<qulonglong>(job.sequence)},
        };
    }
    return rows;
}

QString AuxiliaryJobScheduler::stateName(JobState state)
{
    switch (state) {
    case JobState::Queued: return QStringLiteral("queued");
    case JobState::Running: return QStringLiteral("running");
    case JobState::Completed: return QStringLiteral("completed");
    case JobState::Failed: return QStringLiteral("failed");
    case JobState::Cancelled: return QStringLiteral("cancelled");
    }
    return QStringLiteral("unknown");
}

int AuxiliaryJobScheduler::runningCountForClass(const QString &jobClass) const
{
    int count = 0;
    for (const Job &job : m_jobs)
        if (job.state == JobState::Running && job.jobClass == jobClass)
            ++count;
    return count;
}

bool AuxiliaryJobScheduler::resourceBusy(const QString &resourceKey) const
{
    if (resourceKey.isEmpty()) return false;
    for (const Job &job : m_jobs)
        if (job.state == JobState::Running && job.resourceKey == resourceKey)
            return true;
    return false;
}

int AuxiliaryJobScheduler::bestQueuedIndex() const
{
    int best = -1;
    for (int i = 0; i < m_jobs.size(); ++i) {
        const Job &job = m_jobs.at(i);
        if (job.state != JobState::Queued) continue;
        if (runningCountForClass(job.jobClass) >= classLimit(job.jobClass)) continue;
        if (resourceBusy(job.resourceKey)) continue;
        if (best < 0 || job.priority > m_jobs.at(best).priority
            || (job.priority == m_jobs.at(best).priority
                && job.sequence < m_jobs.at(best).sequence)) {
            best = i;
        }
    }
    return best;
}

AuxiliaryJobScheduler::Job *AuxiliaryJobScheduler::findJob(const QString &id)
{
    for (Job &job : m_jobs)
        if (job.id == id) return &job;
    return nullptr;
}

const AuxiliaryJobScheduler::Job *AuxiliaryJobScheduler::findJob(const QString &id) const
{
    for (const Job &job : m_jobs)
        if (job.id == id) return &job;
    return nullptr;
}

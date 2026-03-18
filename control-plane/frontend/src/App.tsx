import { FormEvent, useCallback, useEffect, useMemo, useRef, useState } from 'react'

import { MiniTrendChart, type TrendLine } from './components/MiniTrendChart'
import { SimpleChart, type ChartSample } from './components/SimpleChart'
import { ApiClient, ApiRequestError } from './lib/api'
import { extractTimestamp, getMetricValue, getNumericMetric, getTextMetric } from './lib/dashboard'
import {
  formatBytes,
  formatChartLabel,
  formatDateTime,
  formatMetricValue,
  formatNumber,
  formatPercent,
  formatRelativeTime,
  formatWindowLabel,
} from './lib/format'
import { LOCALE_STORAGE_KEY, readStoredLocale, t, type Locale } from './lib/i18n'
import type {
  AgentHistoryResponse,
  AgentSummary,
  AgentTrend,
  ApiKey,
  Dashboard,
  GpuMetric,
  MetricPayload,
  TopProcess,
  User,
} from './lib/types'

const TOKEN_STORAGE_KEY = 'btop_token'
const AUTO_REFRESH_INTERVAL_STORAGE_KEY = 'btop_auto_refresh_interval_ms'
const AUTO_REFRESH_INTERVAL_OPTIONS = [5000, 10000, 15000, 30000, 60000] as const
const HISTORY_WINDOWS = [15, 60, 360, 1440]
const CARD_TREND_MINUTES = 60
const CARD_TREND_LIMIT = 18
const DEFAULT_SELECTED_TRENDS = ['cpu', 'memory', 'gpu', 'processes'] as const
const DEFAULT_LOGIN = { username: 'admin', password: 'admin123456' }
const storedToken = window.localStorage.getItem(TOKEN_STORAGE_KEY)
const PAGE_HASHES = {
  control: '#control',
  cards: '#cards',
} as const

type PageMode = 'control' | 'cards'
type StatusFilter = 'all' | 'online' | 'offline'
type TrendKey = 'cpu' | 'memory' | 'gpu' | 'processes'
type ProcessSortKey = 'pid' | 'name' | 'user' | 'cpu_percent' | 'memory_bytes' | 'threads' | 'state'
type SettingsDraft = {
  display_name: string
  desired_collection_interval_ms: string
  desired_enable_gpu: boolean
}

function readStoredAutoRefreshInterval(): number {
  const raw = Number(window.localStorage.getItem(AUTO_REFRESH_INTERVAL_STORAGE_KEY))
  return AUTO_REFRESH_INTERVAL_OPTIONS.includes(raw as (typeof AUTO_REFRESH_INTERVAL_OPTIONS)[number]) ? raw : 30000
}

function readPageModeFromHash(): PageMode {
  return window.location.hash === PAGE_HASHES.cards ? 'cards' : 'control'
}

export default function App() {
  const [locale, setLocale] = useState<Locale>(readStoredLocale())
  const [token, setToken] = useState<string | null>(storedToken)
  const [user, setUser] = useState<User | null>(null)
  const [agents, setAgents] = useState<AgentSummary[]>([])
  const [agentTrends, setAgentTrends] = useState<Record<string, AgentTrend['points']>>({})
  const [history, setHistory] = useState<AgentHistoryResponse | null>(null)
  const [apiKeys, setApiKeys] = useState<ApiKey[]>([])
  const [dashboard, setDashboard] = useState<Dashboard | null>(null)
  const [selectedAgentId, setSelectedAgentId] = useState<string | null>(null)
  const [selectedAgentDetail, setSelectedAgentDetail] = useState<AgentSummary | null>(null)
  const [historyMinutes, setHistoryMinutes] = useState(60)
  const [loadingApp, setLoadingApp] = useState(false)
  const [loadingHistory, setLoadingHistory] = useState(false)
  const [loginPending, setLoginPending] = useState(false)
  const [creatingKey, setCreatingKey] = useState(false)
  const [deletingKeyId, setDeletingKeyId] = useState<string | null>(null)
  const [deletingAgentId, setDeletingAgentId] = useState<string | null>(null)
  const [savingSettings, setSavingSettings] = useState(false)
  const [savingLayout, setSavingLayout] = useState(false)
  const [autoRefresh, setAutoRefresh] = useState(true)
  const [autoRefreshInterval, setAutoRefreshInterval] = useState<number>(readStoredAutoRefreshInterval())
  const [pageMode, setPageMode] = useState<PageMode>(readPageModeFromHash())
  const [statusFilter, setStatusFilter] = useState<StatusFilter>('all')
  const [searchQuery, setSearchQuery] = useState('')
  const [lastSyncedAt, setLastSyncedAt] = useState<string | null>(null)
  const [historyReloadNonce, setHistoryReloadNonce] = useState(0)
  const [selectedTrendKeys, setSelectedTrendKeys] = useState<TrendKey[]>([...DEFAULT_SELECTED_TRENDS])
  const [collapsedAgentIds, setCollapsedAgentIds] = useState<string[]>([])
  const [processSortKey, setProcessSortKey] = useState<ProcessSortKey>('cpu_percent')
  const [processSortDirection, setProcessSortDirection] = useState<'asc' | 'desc'>('desc')
  const [message, setMessage] = useState<{ type: 'error' | 'success'; text: string } | null>(null)
  const [newKeyLabel, setNewKeyLabel] = useState('default-agent')
  const [createdKey, setCreatedKey] = useState<string | null>(null)
  const [copiedKey, setCopiedKey] = useState(false)
  const [layoutName, setLayoutName] = useState('Default')
  const [layoutJson, setLayoutJson] = useState('{"panels": []}')
  const [loginForm, setLoginForm] = useState(DEFAULT_LOGIN)
  const [settingsDraft, setSettingsDraft] = useState<SettingsDraft | null>(null)
  const hydrateRequestIdRef = useRef(0)

  const api = useMemo(() => new ApiClient('/api/v1', storedToken), [])
  const selectedAgentSummary = agents.find((agent) => agent.id === selectedAgentId) ?? null
  const selectedAgent =
    selectedAgentDetail && selectedAgentDetail.id === selectedAgentId ? selectedAgentDetail : selectedAgentSummary
  const historyPoints = history?.agent_id === selectedAgentId ? history.points : []
  const selectedGpus = getGpuList(selectedAgent?.latest_payload)
  const selectedProcesses = useMemo(
    () => sortProcesses(getProcessList(selectedAgent?.latest_payload), processSortKey, processSortDirection),
    [processSortDirection, processSortKey, selectedAgent?.latest_payload],
  )

  const apiKeyList = useMemo(
    () => [...apiKeys].sort((left, right) => right.created_at.localeCompare(left.created_at)),
    [apiKeys],
  )

  const filteredAgents = useMemo(() => {
    const query = searchQuery.trim().toLowerCase()
    return agents.filter((agent) => {
      if (statusFilter !== 'all' && agent.status !== statusFilter) {
        return false
      }
      if (!query) {
        return true
      }
      const haystack = [agent.hostname, agent.display_name ?? '', agent.owner_user_id ?? '']
        .join(' ')
        .toLowerCase()
      return haystack.includes(query)
    })
  }, [agents, searchQuery, statusFilter])

  const fleetStats = useMemo(() => {
    const online = agents.filter((agent) => agent.status === 'online').length
    const offline = agents.filter((agent) => agent.status === 'offline').length
    const gpuCapable = agents.filter((agent) => getGpuList(agent.latest_payload).length > 0).length
    return { total: agents.length, online, offline, gpuCapable }
  }, [agents])

  const historyCards = useMemo(() => buildHistoryCards(locale).filter((card) => selectedTrendKeys.includes(card.id)), [locale, selectedTrendKeys])

  useEffect(() => {
    api.setToken(token)
  }, [api, token])

  useEffect(() => {
    window.localStorage.setItem(LOCALE_STORAGE_KEY, locale)
  }, [locale])

  useEffect(() => {
    window.localStorage.setItem(AUTO_REFRESH_INTERVAL_STORAGE_KEY, String(autoRefreshInterval))
  }, [autoRefreshInterval])

  useEffect(() => {
    const handleHashChange = () => {
      setPageMode(readPageModeFromHash())
    }
    window.addEventListener('hashchange', handleHashChange)
    return () => window.removeEventListener('hashchange', handleHashChange)
  }, [])

  const resetSession = useCallback((text?: string) => {
    window.localStorage.removeItem(TOKEN_STORAGE_KEY)
    setToken(null)
    setUser(null)
    setAgents([])
    setAgentTrends({})
    setHistory(null)
    setApiKeys([])
    setDashboard(null)
    setSelectedAgentId(null)
    setSelectedAgentDetail(null)
    setLastSyncedAt(null)
    setSettingsDraft(null)
    setCreatedKey(null)
    setCopiedKey(false)
    setMessage(text ? { type: 'error', text } : null)
  }, [])

  const handleRequestError = useCallback(
    (error: unknown, fallback: string, options?: { resetOnUnauthorized?: boolean }) => {
      if (error instanceof ApiRequestError && error.status === 401 && options?.resetOnUnauthorized !== false) {
        const text = t(locale, 'sessionExpired')
        resetSession(text)
        return text
      }
      return error instanceof Error ? error.message : fallback
    },
    [locale, resetSession],
  )

  const hydrate = useCallback(async () => {
    if (!token) {
      return
    }

    const requestId = ++hydrateRequestIdRef.current
    setLoadingApp(true)
    try {
      const [me, nextAgents, nextTrends, nextKeys, nextDashboard] = await Promise.all([
        api.me(),
        api.listAgents(),
        api.listAgentTrends(CARD_TREND_MINUTES, CARD_TREND_LIMIT),
        api.listApiKeys(),
        api.getDefaultDashboard(),
      ])
      if (requestId !== hydrateRequestIdRef.current) {
        return
      }
      setUser(me)
      setAgents(nextAgents)
      setAgentTrends(Object.fromEntries(nextTrends.map((trend) => [trend.agent_id, trend.points])))
      setApiKeys(nextKeys)
      setDashboard(nextDashboard)
      setLayoutName(nextDashboard.name)
      setLayoutJson(JSON.stringify(nextDashboard.layout, null, 2))
      setSelectedAgentId((current) => {
        if (current && nextAgents.some((agent) => agent.id === current)) {
          return current
        }
        return nextAgents[0]?.id ?? null
      })
      setSelectedAgentDetail((current) => (current && nextAgents.some((agent) => agent.id === current.id) ? current : null))
      setLastSyncedAt(new Date().toISOString())
      setHistoryReloadNonce((current) => current + 1)
      setMessage(null)
    } catch (error) {
      if (requestId === hydrateRequestIdRef.current) {
        setMessage({ type: 'error', text: handleRequestError(error, t(locale, 'refreshFailed')) })
      }
    } finally {
      if (requestId === hydrateRequestIdRef.current) {
        setLoadingApp(false)
      }
    }
  }, [api, handleRequestError, locale, token])

  useEffect(() => {
    if (!token) {
      return
    }
    void hydrate()
  }, [hydrate, token])

  useEffect(() => {
    if (!token || !autoRefresh) {
      return
    }
    const timer = window.setInterval(() => {
      void hydrate()
    }, autoRefreshInterval)
    return () => window.clearInterval(timer)
  }, [autoRefresh, autoRefreshInterval, hydrate, token])

  useEffect(() => {
    if (!token || !selectedAgentId) {
      setHistory(null)
      return
    }

    let cancelled = false
    setLoadingHistory(true)
    void api
      .getAgentHistory(selectedAgentId, historyMinutes)
      .then((response) => {
        if (cancelled) {
          return
        }
        setHistory(response)
      })
      .catch((error) => {
        if (cancelled) {
          return
        }
        setHistory(null)
        setMessage({ type: 'error', text: handleRequestError(error, t(locale, 'historyLoadFailed')) })
      })
      .finally(() => {
        if (!cancelled) {
          setLoadingHistory(false)
        }
      })

    return () => {
      cancelled = true
    }
  }, [api, handleRequestError, historyMinutes, historyReloadNonce, selectedAgentId, token])

  useEffect(() => {
    if (!token || !selectedAgentId) {
      setSelectedAgentDetail(null)
      return
    }

    let cancelled = false
    void api
      .getAgent(selectedAgentId)
      .then((response) => {
        if (!cancelled) {
          setSelectedAgentDetail(response)
        }
      })
      .catch((error) => {
        if (cancelled) {
          return
        }
        setSelectedAgentDetail(null)
        setMessage({ type: 'error', text: handleRequestError(error, t(locale, 'deviceLoadFailed')) })
      })

    return () => {
      cancelled = true
    }
  }, [api, handleRequestError, historyReloadNonce, locale, selectedAgentId, token])

  useEffect(() => {
    if (!selectedAgent) {
      setSettingsDraft(null)
      return
    }
    setSettingsDraft({
      display_name: selectedAgent.display_name ?? selectedAgent.hostname,
      desired_collection_interval_ms: String(selectedAgent.desired_collection_interval_ms),
      desired_enable_gpu: selectedAgent.desired_enable_gpu,
    })
  }, [selectedAgent])

  async function onLogin(event: FormEvent<HTMLFormElement>) {
    event.preventDefault()
    setLoginPending(true)
    try {
      const response = await api.login(loginForm)
      window.localStorage.setItem(TOKEN_STORAGE_KEY, response.access_token)
      setToken(response.access_token)
      setUser(response.user)
      setMessage(null)
    } catch (error) {
      setMessage({ type: 'error', text: handleRequestError(error, t(locale, 'loginFailed'), { resetOnUnauthorized: false }) })
    } finally {
      setLoginPending(false)
    }
  }

  async function onCreateApiKey() {
    if (!newKeyLabel.trim()) {
      return
    }
    setCreatingKey(true)
    try {
      const response = await api.createApiKey(newKeyLabel.trim())
      setApiKeys((current) => [response.key, ...current])
      setCreatedKey(response.api_key)
      setCopiedKey(false)
      setMessage(null)
    } catch (error) {
      setMessage({ type: 'error', text: handleRequestError(error, t(locale, 'createKeyFailed')) })
    } finally {
      setCreatingKey(false)
    }
  }

  async function onCopyKey() {
    if (!createdKey || !window.navigator.clipboard) {
      return
    }
    try {
      await window.navigator.clipboard.writeText(createdKey)
      setCopiedKey(true)
    } catch {
      setMessage({ type: 'error', text: t(locale, 'clipboardFailed') })
    }
  }

  async function onSaveSettings() {
    if (!selectedAgent || !settingsDraft) {
      return
    }
    const interval = Number(settingsDraft.desired_collection_interval_ms)
    if (!Number.isFinite(interval) || interval < 250) {
      setMessage({ type: 'error', text: t(locale, 'minIntervalError') })
      return
    }

    setSavingSettings(true)
    try {
      const updated = await api.updateAgentSettings(selectedAgent.id, {
        display_name: settingsDraft.display_name.trim(),
        desired_collection_interval_ms: interval,
        desired_enable_gpu: settingsDraft.desired_enable_gpu,
      })
      setAgents((current) =>
        current.map((agent) =>
          agent.id === selectedAgent.id
            ? {
                ...agent,
                display_name: updated.display_name,
                desired_collection_interval_ms: updated.desired_collection_interval_ms,
                desired_enable_gpu: updated.desired_enable_gpu,
              }
            : agent,
        ),
      )
      setSelectedAgentDetail((current) =>
        current && current.id === selectedAgent.id
          ? {
              ...current,
              display_name: updated.display_name,
              desired_collection_interval_ms: updated.desired_collection_interval_ms,
              desired_enable_gpu: updated.desired_enable_gpu,
            }
          : current,
      )
      setMessage({ type: 'success', text: t(locale, 'settingsSaved') })
    } catch (error) {
      setMessage({ type: 'error', text: handleRequestError(error, t(locale, 'settingsSaveFailed')) })
    } finally {
      setSavingSettings(false)
    }
  }

  async function onDeleteSelectedAgent() {
    if (!selectedAgent) {
      return
    }
    if (!window.confirm(t(locale, 'deleteDeviceConfirm'))) {
      return
    }

    setDeletingAgentId(selectedAgent.id)
    try {
      await api.deleteAgent(selectedAgent.id)
      const nextAgents = agents.filter((agent) => agent.id !== selectedAgent.id)
      setAgents(nextAgents)
      setSelectedAgentId((currentSelected) => {
        if (currentSelected !== selectedAgent.id) {
          return currentSelected
        }
        return nextAgents[0]?.id ?? null
      })
      setSelectedAgentDetail((current) => (current?.id === selectedAgent.id ? null : current))
      setAgentTrends((current) => {
        const next = { ...current }
        delete next[selectedAgent.id]
        return next
      })
      setHistory((current) => (current?.agent_id === selectedAgent.id ? null : current))
      setMessage({ type: 'success', text: t(locale, 'deviceDeleted') })
    } catch (error) {
      setMessage({ type: 'error', text: handleRequestError(error, t(locale, 'deviceDeleteFailed')) })
    } finally {
      setDeletingAgentId(null)
    }
  }

  async function onDeleteApiKey(key: ApiKey) {
    if (!window.confirm(t(locale, 'deleteKeyConfirm'))) {
      return
    }

    setDeletingKeyId(key.id)
    try {
      await api.deleteApiKey(key.id)
      setApiKeys((current) => current.filter((item) => item.id !== key.id))
      setMessage({ type: 'success', text: t(locale, 'keyDeleted') })
    } catch (error) {
      setMessage({ type: 'error', text: handleRequestError(error, t(locale, 'keyDeleteFailed')) })
    } finally {
      setDeletingKeyId(null)
    }
  }

  async function onSaveLayout() {
    try {
      const parsed = JSON.parse(layoutJson) as Dashboard['layout']
      setSavingLayout(true)
      const updated = await api.saveDefaultDashboard(parsed, layoutName.trim() || 'Default')
      setDashboard(updated)
      setLayoutName(updated.name)
      setLayoutJson(JSON.stringify(updated.layout, null, 2))
      setMessage(null)
    } catch (error) {
      if (error instanceof SyntaxError) {
        setMessage({ type: 'error', text: t(locale, 'invalidJson') })
      } else {
        setMessage({ type: 'error', text: handleRequestError(error, t(locale, 'layoutSaveFailed')) })
      }
    } finally {
      setSavingLayout(false)
    }
  }

  function onLogout() {
    resetSession()
  }

  function toggleTrendSelection(key: TrendKey) {
    setSelectedTrendKeys((current) => {
      if (current.includes(key)) {
        return current.filter((item) => item !== key)
      }
      return [...current, key]
    })
  }

  function toggleTrendSelectionForAgent(agentId: string, key: TrendKey) {
    setSelectedAgentId(agentId)
    toggleTrendSelection(key)
  }

  function toggleAgentCollapsed(agentId: string) {
    setCollapsedAgentIds((current) => (current.includes(agentId) ? current.filter((item) => item !== agentId) : [...current, agentId]))
  }

  function handleProcessSort(key: ProcessSortKey) {
    if (processSortKey === key) {
      setProcessSortDirection((current) => (current === 'asc' ? 'desc' : 'asc'))
      return
    }
    setProcessSortKey(key)
    setProcessSortDirection(key === 'name' || key === 'user' || key === 'state' ? 'asc' : 'desc')
  }

  function navigateToPage(nextPage: PageMode) {
    window.location.hash = PAGE_HASHES[nextPage]
  }

  if (!token) {
    return (
      <div className="login-shell">
        <form className="login-card" onSubmit={onLogin}>
          <div className="language-switch login-language">
            <span>{t(locale, 'language')}</span>
            <select value={locale} onChange={(event) => setLocale(event.target.value as Locale)}>
              <option value="zh-CN">{t(locale, 'chinese')}</option>
              <option value="en-US">{t(locale, 'english')}</option>
            </select>
          </div>
          <p className="eyebrow">{t(locale, 'appName')}</p>
          <h1>{t(locale, 'signIn')}</h1>
          <p className="muted">{t(locale, 'appSubtitle')}</p>
          <label>
            {t(locale, 'username')}
            <input value={loginForm.username} onChange={(event) => setLoginForm((current) => ({ ...current, username: event.target.value }))} />
          </label>
          <label>
            {t(locale, 'password')}
            <input
              type="password"
              value={loginForm.password}
              onChange={(event) => setLoginForm((current) => ({ ...current, password: event.target.value }))}
            />
          </label>
          <button type="submit" disabled={loginPending}>
            {loginPending ? t(locale, 'signingIn') : t(locale, 'signInAction')}
          </button>
          {message ? <div className={`banner ${message.type}`}>{message.text}</div> : null}
        </form>
      </div>
    )
  }

  return (
    <div className="app-shell">
      <header className="topbar panel">
        <div className="topbar-copy">
          <p className="eyebrow">{t(locale, 'appName')}</p>
          <h1>{t(locale, 'fleetOverview')}</h1>
          <p className="muted">{t(locale, 'appSubtitle')}</p>
        </div>
        <div className="topbar-rail">
          <div className="topbar-meta-row">
            <label className="toggle">
              <input checked={autoRefresh} onChange={(event) => setAutoRefresh(event.target.checked)} type="checkbox" />
              {t(locale, 'autoRefresh')}
            </label>
            <label className="field-chip">
              <span>{t(locale, 'refreshInterval')}</span>
              <select
                value={autoRefreshInterval}
                onChange={(event) => setAutoRefreshInterval(Number(event.target.value))}
                disabled={!autoRefresh}
              >
                {AUTO_REFRESH_INTERVAL_OPTIONS.map((interval) => (
                  <option key={interval} value={interval}>
                    {formatRefreshIntervalOption(interval, locale)}
                  </option>
                ))}
              </select>
            </label>
            {lastSyncedAt ? (
              <div className="badge">
                {t(locale, 'lastSynced')}: {formatRelativeTime(lastSyncedAt, locale)}
              </div>
            ) : null}
          </div>
          <div className="topbar-action-row">
            <div className="segmented">
              <button
                type="button"
                className={pageMode === 'control' ? 'secondary active' : 'secondary'}
                onClick={() => navigateToPage('control')}
              >
                {t(locale, 'controlView')}
              </button>
              <button
                type="button"
                className={pageMode === 'cards' ? 'secondary active' : 'secondary'}
                onClick={() => navigateToPage('cards')}
              >
                {t(locale, 'cardsView')}
              </button>
            </div>
            <label className="field-chip">
              <span>{t(locale, 'language')}</span>
              <select value={locale} onChange={(event) => setLocale(event.target.value as Locale)}>
                <option value="zh-CN">{t(locale, 'chinese')}</option>
                <option value="en-US">{t(locale, 'english')}</option>
              </select>
            </label>
            <div className="user-chip">
              {user?.username}
              {user?.is_superuser ? <span className="inline-tag">{t(locale, 'adminOnlyTag')}</span> : null}
            </div>
            <button type="button" onClick={() => void hydrate()} disabled={loadingApp}>
              {loadingApp ? t(locale, 'refreshing') : t(locale, 'refreshNow')}
            </button>
            <button type="button" className="secondary" onClick={onLogout}>
              {t(locale, 'logout')}
            </button>
          </div>
        </div>
      </header>

      {message ? <div className={`banner ${message.type}`}>{message.text}</div> : null}

      {pageMode === 'control' ? (
      <section className="panel overview-panel">
        <div className="stat-grid">
          <StatCard label={t(locale, 'totalDevices')} value={String(fleetStats.total)} />
          <StatCard label={t(locale, 'onlineDevices')} value={String(fleetStats.online)} />
          <StatCard label={t(locale, 'offlineDevices')} value={String(fleetStats.offline)} />
          <StatCard label={t(locale, 'gpuDevices')} value={String(fleetStats.gpuCapable)} />
        </div>
        <div className="filter-row">
          <input
            placeholder={t(locale, 'searchPlaceholder')}
            value={searchQuery}
            onChange={(event) => setSearchQuery(event.target.value)}
          />
          <div className="segmented">
            {(['all', 'online', 'offline'] as StatusFilter[]).map((value) => (
              <button
                key={value}
                type="button"
                className={statusFilter === value ? 'secondary active' : 'secondary'}
                onClick={() => setStatusFilter(value)}
              >
                {value === 'all'
                  ? t(locale, 'statusAll')
                  : value === 'online'
                    ? t(locale, 'statusOnline')
                    : t(locale, 'statusOffline')}
              </button>
            ))}
          </div>
        </div>
        <div className="scope-row muted">{user?.is_superuser ? t(locale, 'adminScope') : t(locale, 'userScope')}</div>
      </section>
      ) : (
      <section className="panel overview-panel compact-overview-panel">
        <div className="filter-row">
          <input
            placeholder={t(locale, 'searchPlaceholder')}
            value={searchQuery}
            onChange={(event) => setSearchQuery(event.target.value)}
          />
          <div className="segmented">
            {(['all', 'online', 'offline'] as StatusFilter[]).map((value) => (
              <button
                key={value}
                type="button"
                className={statusFilter === value ? 'secondary active' : 'secondary'}
                onClick={() => setStatusFilter(value)}
              >
                {value === 'all'
                  ? t(locale, 'statusAll')
                  : value === 'online'
                    ? t(locale, 'statusOnline')
                    : t(locale, 'statusOffline')}
              </button>
            ))}
          </div>
        </div>
      </section>
      )}

      <section className="device-grid">
        {filteredAgents.map((agent) => (
          <DeviceCard
            key={agent.id}
            agent={agent}
            isSelected={agent.id === selectedAgentId}
            isCollapsed={collapsedAgentIds.includes(agent.id)}
            locale={locale}
            selectedTrendKeys={selectedTrendKeys}
            trendPoints={agentTrends[agent.id] ?? []}
            showOwner={Boolean(user?.is_superuser)}
            onSelect={() => setSelectedAgentId(agent.id)}
            onToggleCollapse={() => toggleAgentCollapsed(agent.id)}
            onToggleTrend={(key) => toggleTrendSelectionForAgent(agent.id, key)}
          />
        ))}
        {!agents.length ? <div className="panel empty-panel">{t(locale, 'noDevices')}</div> : null}
        {agents.length > 0 && !filteredAgents.length ? <div className="panel empty-panel">{t(locale, 'noMatchingDevices')}</div> : null}
      </section>

      {pageMode === 'control' ? (
      <section className="detail-layout">
        <div className="detail-main">
          <section className="panel detail-hero">
            <div className="panel-header">
              <div>
                <h2>{selectedAgent ? selectedAgent.display_name ?? selectedAgent.hostname : t(locale, 'selectDevice')}</h2>
                <p className="muted">{t(locale, 'selectedDeviceHint')}</p>
              </div>
              {selectedAgent ? (
                <div className="hero-meta">
                  <span className={`status-chip ${selectedAgent.status}`}>{statusLabel(selectedAgent.status, locale)}</span>
                  <span className="badge">{t(locale, 'latestPayload')}: {formatDateTime(selectedAgent.latest_payload_at, locale)}</span>
                </div>
              ) : null}
            </div>
            {selectedAgent ? (
              <div className="detail-summary-grid">
                <MetricCell label={t(locale, 'cpuUsage')} value={formatPercent(getNumericMetric(selectedAgent.latest_payload, 'cpu.usage_percent'), locale)} />
                <MetricCell label={t(locale, 'memoryUsage')} value={formatMemorySummary(selectedAgent.latest_payload, locale)} />
                <MetricCell label={t(locale, 'networkIo')} value={formatNetworkSummary(selectedAgent.latest_payload, locale)} />
                <MetricCell label={t(locale, 'gpuSummary')} value={formatGpuSummary(selectedAgent.latest_payload, locale)} />
                <MetricCell label={t(locale, 'processCount')} value={formatNumber(getNumericMetric(selectedAgent.latest_payload, 'processes.total_processes'), locale)} />
                <MetricCell label={t(locale, 'loadAverage')} value={formatNumber(getNumericMetric(selectedAgent.latest_payload, 'processes.load_average_1min'), locale)} />
              </div>
            ) : (
              <div className="empty-panel">{t(locale, 'selectDevice')}</div>
            )}
          </section>

          <section className="chart-grid">
            {historyCards.map((card) => (
              <article className="panel chart-panel" key={card.key}>
                <div className="chart-header-row">
                  <div>
                    <h3>{card.title}</h3>
                    <p className="muted">{t(locale, 'historyWindow')}: {formatWindowLabel(historyMinutes, locale)}</p>
                  </div>
                  <strong>{formatMetricValue(card.key, latestMetricValue(selectedAgent?.latest_payload, card.key), locale)}</strong>
                </div>
                <SimpleChart
                  accent={card.accent}
                  ariaLabel={card.title}
                  emptyLabel={t(locale, 'chartEmpty')}
                  samples={buildChartSamples(historyPoints, card.key, locale)}
                />
              </article>
            ))}
            {!historyCards.length ? <div className="panel empty-panel">{t(locale, 'noTrendSelected')}</div> : null}
          </section>

          <section className="panel history-toolbar">
            <div className="history-toolbar-main">
              <div className="segmented">
                {HISTORY_WINDOWS.map((windowMinutes) => (
                  <button
                    key={windowMinutes}
                    type="button"
                    className={historyMinutes === windowMinutes ? 'secondary active' : 'secondary'}
                    onClick={() => setHistoryMinutes(windowMinutes)}
                  >
                    {formatWindowLabel(windowMinutes, locale)}
                  </button>
                ))}
              </div>
              <div className="trend-toggle-row">
                {buildHistoryCards(locale).map((card) => (
                  <button
                    key={card.id}
                    type="button"
                    className={selectedTrendKeys.includes(card.id) ? 'secondary active trend-toggle' : 'secondary trend-toggle'}
                    onClick={() => toggleTrendSelection(card.id)}
                  >
                    <span
                      className={selectedTrendKeys.includes(card.id) ? 'metric-toggle-dot active' : 'metric-toggle-dot'}
                      style={{ ['--toggle-color' as string]: card.accent }}
                    />
                    {card.title}
                  </button>
                ))}
              </div>
            </div>
            <div className="muted">{loadingHistory ? t(locale, 'refreshing') : `${historyPoints.length} ${t(locale, 'samples')}`}</div>
          </section>

          <section className="panel table-panel">
            <div className="panel-header">
              <div>
                <h3>{t(locale, 'gpuDetails')}</h3>
                <p className="muted">{selectedGpus.length ? `${selectedGpus.length} · ${t(locale, 'gpuCount')}` : t(locale, 'gpuNone')}</p>
              </div>
            </div>
            {selectedGpus.length ? (
              <div className="table-wrap">
                <table>
                  <thead>
                    <tr>
                      <th>{t(locale, 'gpuTableName')}</th>
                      <th>{t(locale, 'gpuTableUtil')}</th>
                      <th>{t(locale, 'gpuTableMemory')}</th>
                      <th>{t(locale, 'gpuTablePower')}</th>
                      <th>{t(locale, 'gpuTableTemp')}</th>
                      <th>{t(locale, 'gpuTableCodec')}</th>
                    </tr>
                  </thead>
                  <tbody>
                    {selectedGpus.map((gpu) => (
                      <tr key={gpu.index}>
                        <td>GPU {gpu.index}</td>
                        <td>{formatPercent(gpu.utilization_percent, locale)}</td>
                        <td>{formatGpuMemory(gpu, locale)}</td>
                        <td>{formatNumber(gpu.power_usage_watts, locale)} W</td>
                        <td>{formatNumber(gpu.temperature_celsius, locale)}°C</td>
                        <td>
                          {formatPercent(gpu.encoder_utilization_percent, locale)} / {formatPercent(gpu.decoder_utilization_percent, locale)}
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            ) : (
              <div className="empty-panel">{t(locale, 'gpuNone')}</div>
            )}
          </section>

          <section className="panel table-panel">
            <div className="panel-header">
              <div>
                <h3>{t(locale, 'processDetails')}</h3>
                <p className="muted">{formatNumber(getNumericMetric(selectedAgent?.latest_payload, 'processes.total_processes'), locale)}</p>
              </div>
            </div>
            {selectedProcesses.length ? (
              <div className="table-wrap process-table-wrap">
                <table>
                  <thead>
                    <tr>
                      <SortableHeader
                        label={t(locale, 'processTablePid')}
                        sortKey="pid"
                        activeKey={processSortKey}
                        direction={processSortDirection}
                        onSort={handleProcessSort}
                      />
                      <SortableHeader
                        label={t(locale, 'processTableName')}
                        sortKey="name"
                        activeKey={processSortKey}
                        direction={processSortDirection}
                        onSort={handleProcessSort}
                      />
                      <SortableHeader
                        label={t(locale, 'processTableUser')}
                        sortKey="user"
                        activeKey={processSortKey}
                        direction={processSortDirection}
                        onSort={handleProcessSort}
                      />
                      <SortableHeader
                        label={t(locale, 'processTableCpu')}
                        sortKey="cpu_percent"
                        activeKey={processSortKey}
                        direction={processSortDirection}
                        onSort={handleProcessSort}
                      />
                      <SortableHeader
                        label={t(locale, 'processTableMemory')}
                        sortKey="memory_bytes"
                        activeKey={processSortKey}
                        direction={processSortDirection}
                        onSort={handleProcessSort}
                      />
                      <SortableHeader
                        label={t(locale, 'processTableThreads')}
                        sortKey="threads"
                        activeKey={processSortKey}
                        direction={processSortDirection}
                        onSort={handleProcessSort}
                      />
                      <SortableHeader
                        label={t(locale, 'processTableState')}
                        sortKey="state"
                        activeKey={processSortKey}
                        direction={processSortDirection}
                        onSort={handleProcessSort}
                      />
                    </tr>
                  </thead>
                  <tbody>
                    {selectedProcesses.map((process) => (
                      <tr key={process.pid}>
                        <td>{process.pid}</td>
                        <td>{process.name}</td>
                        <td>{process.user ?? '--'}</td>
                        <td>{formatPercent(process.cpu_percent, locale)}</td>
                        <td>{formatBytes(process.memory_bytes, locale)}</td>
                        <td>{formatNumber(process.threads, locale)}</td>
                        <td>{process.state ?? '--'}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            ) : (
              <div className="empty-panel">{t(locale, 'none')}</div>
            )}
          </section>

          <section className="panel raw-panel">
            <div className="panel-header">
              <div>
                <h3>{t(locale, 'rawPayload')}</h3>
                <p className="muted">{t(locale, 'rawPayloadHint')}</p>
              </div>
            </div>
            <pre>{JSON.stringify(selectedAgent?.latest_payload ?? null, null, 2)}</pre>
          </section>
        </div>

        <aside className="detail-side">
          <section className="panel settings-panel">
            <div className="panel-header">
              <div>
                <h3>{t(locale, 'settings')}</h3>
                <p className="muted">{selectedAgent?.hostname ?? t(locale, 'selectDevice')}</p>
              </div>
            </div>
            {settingsDraft ? (
              <div className="stack">
                <label>
                  {t(locale, 'displayName')}
                  <input
                    value={settingsDraft.display_name}
                    onChange={(event) =>
                      setSettingsDraft((current) =>
                        current ? { ...current, display_name: event.target.value } : current,
                      )
                    }
                  />
                </label>
                <label>
                  {t(locale, 'collectionInterval')}
                  <input
                    type="number"
                    min={250}
                    step={250}
                    value={settingsDraft.desired_collection_interval_ms}
                    onChange={(event) =>
                      setSettingsDraft((current) =>
                        current ? { ...current, desired_collection_interval_ms: event.target.value } : current,
                      )
                    }
                  />
                </label>
                <label className="checkbox-row">
                  <input
                    type="checkbox"
                    checked={settingsDraft.desired_enable_gpu}
                    onChange={(event) =>
                      setSettingsDraft((current) =>
                        current ? { ...current, desired_enable_gpu: event.target.checked } : current,
                      )
                    }
                  />
                  <span>{t(locale, 'enableGpu')}</span>
                </label>
                <button type="button" onClick={() => void onSaveSettings()} disabled={savingSettings || !selectedAgent}>
                  {savingSettings ? t(locale, 'savingSettings') : t(locale, 'saveSettings')}
                </button>
                <p className="muted small-text">{t(locale, 'settingsSyncHint')}</p>
                <button
                  type="button"
                  className="secondary danger"
                  onClick={() => void onDeleteSelectedAgent()}
                  disabled={deletingAgentId === selectedAgent?.id || !selectedAgent}
                >
                  {deletingAgentId === selectedAgent?.id ? t(locale, 'deletingDevice') : t(locale, 'deleteDevice')}
                </button>
              </div>
            ) : (
              <div className="empty-panel">{t(locale, 'selectDevice')}</div>
            )}
          </section>

          <section className="panel api-panel">
            <div className="panel-header">
              <div>
                <h3>{t(locale, 'apiKeys')}</h3>
                <p className="muted">{t(locale, 'apiKeysHint')}</p>
              </div>
            </div>
            <div className="stack">
              <label>
                {t(locale, 'keyLabel')}
                <input value={newKeyLabel} onChange={(event) => setNewKeyLabel(event.target.value)} />
              </label>
              <button type="button" onClick={() => void onCreateApiKey()} disabled={creatingKey || !newKeyLabel.trim()}>
                {creatingKey ? t(locale, 'creatingKey') : t(locale, 'createKey')}
              </button>
              {createdKey ? (
                <div className="secret-box">
                  <code>{createdKey}</code>
                  <div className="secret-actions">
                    <button type="button" className="secondary" onClick={() => void onCopyKey()}>
                      {copiedKey ? t(locale, 'copied') : t(locale, 'copySecret')}
                    </button>
                    <button type="button" className="secondary" onClick={() => setCreatedKey(null)}>
                      {t(locale, 'dismiss')}
                    </button>
                  </div>
                  <p className="muted">{t(locale, 'rawKeyHint')}</p>
                </div>
              ) : null}
              <div className="key-list">
                {apiKeyList.map((key) => (
                  <div className="key-card" key={key.id}>
                    <div className="panel-header compact">
                      <strong>{key.label}</strong>
                      <span className={key.revoked_at ? 'badge warning' : 'badge'}>
                        {key.revoked_at ? t(locale, 'revoked') : t(locale, 'active')}
                      </span>
                    </div>
                    <div>{key.key_prefix}</div>
                    <div className="muted small-text">{formatDateTime(key.created_at, locale)}</div>
                    <div className="muted small-text">{formatRelativeTime(key.last_used_at, locale)}</div>
                    <button
                      type="button"
                      className="secondary danger key-action"
                      onClick={() => void onDeleteApiKey(key)}
                      disabled={deletingKeyId === key.id}
                    >
                      {deletingKeyId === key.id ? t(locale, 'deletingKey') : t(locale, 'deleteKey')}
                    </button>
                  </div>
                ))}
                {!apiKeyList.length ? <div className="empty-panel">{t(locale, 'noKeys')}</div> : null}
              </div>
            </div>
          </section>

          <section className="panel layout-panel-compact">
            <div className="panel-header">
              <div>
                <h3>{t(locale, 'layout')}</h3>
                <p className="muted">{t(locale, 'layoutHint')}</p>
              </div>
            </div>
            <div className="stack">
              <label>
                {t(locale, 'layoutName')}
                <input value={layoutName} onChange={(event) => setLayoutName(event.target.value)} />
              </label>
              <label>
                {t(locale, 'layoutJson')}
                <textarea value={layoutJson} onChange={(event) => setLayoutJson(event.target.value)} />
              </label>
              <button type="button" onClick={() => void onSaveLayout()} disabled={savingLayout}>
                {savingLayout ? t(locale, 'savingLayout') : t(locale, 'saveLayout')}
              </button>
            </div>
          </section>
        </aside>
      </section>
      ) : null}
    </div>
  )
}

function formatRefreshIntervalOption(intervalMs: number, locale: Locale): string {
  const seconds = intervalMs / 1000
  return locale === 'zh-CN' ? `${seconds} 秒` : `${seconds}s`
}

type StatCardProps = {
  label: string
  value: string
}

function StatCard({ label, value }: StatCardProps) {
  return (
    <div className="stat-card">
      <span>{label}</span>
      <strong>{value}</strong>
    </div>
  )
}

type MetricCellProps = {
  label: string
  value: string
  selected?: boolean
  onClick?: () => void
  color?: string
}

function MetricCell({ label, value, selected = false, onClick, color }: MetricCellProps) {
  const className = selected ? 'metric-cell metric-cell-button active' : 'metric-cell metric-cell-button'
  if (onClick) {
    return (
      <button type="button" className={className} onClick={onClick}>
        <span className="metric-cell-label">
          <span className={selected ? 'metric-toggle-dot active' : 'metric-toggle-dot'} style={{ ['--toggle-color' as string]: color ?? 'var(--accent)' }} />
          {label}
        </span>
        <strong>{value}</strong>
      </button>
    )
  }
  return (
    <div className="metric-cell">
      <span>{label}</span>
      <strong>{value}</strong>
    </div>
  )
}

type DeviceCardProps = {
  agent: AgentSummary
  isSelected: boolean
  isCollapsed: boolean
  locale: Locale
  selectedTrendKeys: TrendKey[]
  trendPoints: AgentTrend['points']
  showOwner: boolean
  onSelect: () => void
  onToggleCollapse: () => void
  onToggleTrend: (key: TrendKey) => void
}

function DeviceCard({
  agent,
  isSelected,
  isCollapsed,
  locale,
  selectedTrendKeys,
  trendPoints,
  showOwner,
  onSelect,
  onToggleCollapse,
  onToggleTrend,
}: DeviceCardProps) {
  const displayName = agent.display_name ?? agent.hostname
  return (
    <article className={isSelected ? `device-card active${isCollapsed ? ' collapsed' : ''}` : `device-card${isCollapsed ? ' collapsed' : ''}`} onClick={onSelect}>
      <div className="device-card-header">
        <button type="button" className="device-card-title" onClick={onSelect}>
          <div>
            <strong>{displayName}</strong>
            <div className="muted small-text">{agent.hostname}</div>
          </div>
        </button>
        <div className="device-card-actions">
          <span className={`status-chip ${agent.status}`}>{statusLabel(agent.status, locale)}</span>
          <button
            type="button"
            className="secondary compact-button"
            onClick={(event) => {
              event.stopPropagation()
              onToggleCollapse()
            }}
          >
            {isCollapsed ? t(locale, 'expandCard') : t(locale, 'collapseCard')}
          </button>
        </div>
      </div>
      {!isCollapsed ? (
        <div className="device-metrics">
          <MetricCell
            label={t(locale, 'cpuUsage')}
            value={formatPercent(getNumericMetric(agent.latest_payload, 'cpu.usage_percent'), locale)}
            selected={selectedTrendKeys.includes('cpu')}
            color="#73e0ce"
            onClick={() => onToggleTrend('cpu')}
          />
          <MetricCell
            label={t(locale, 'memoryUsage')}
            value={formatMemorySummary(agent.latest_payload, locale)}
            selected={selectedTrendKeys.includes('memory')}
            color="#bdf45f"
            onClick={() => onToggleTrend('memory')}
          />
          <MetricCell
            label={t(locale, 'gpuSummary')}
            value={formatGpuSummary(agent.latest_payload, locale)}
            selected={selectedTrendKeys.includes('gpu')}
            color="#ffb86b"
            onClick={() => onToggleTrend('gpu')}
          />
          <MetricCell
            label={t(locale, 'processCount')}
            value={formatNumber(getNumericMetric(agent.latest_payload, 'processes.total_processes'), locale)}
            selected={selectedTrendKeys.includes('processes')}
            color="#7ab6ff"
            onClick={() => onToggleTrend('processes')}
          />
          <MetricCell label={t(locale, 'topProcess')} value={getTopProcesses(agent.latest_payload)[0]?.name ?? t(locale, 'none')} />
          <MetricCell label={t(locale, 'lastSeen')} value={formatRelativeTime(agent.last_seen_at, locale)} />
        </div>
      ) : null}
      <button type="button" className="device-card-trend align-left-button" onClick={onSelect}>
        <div className="device-card-trend-header">
          <span>{t(locale, 'cardTrend')}</span>
          <span>{formatWindowLabel(CARD_TREND_MINUTES, locale)}</span>
        </div>
        <MiniTrendChart
          ariaLabel={`${displayName} ${t(locale, 'cardTrend')}`}
          emptyLabel={t(locale, 'trendUnavailable')}
          lines={buildCardTrendLines(trendPoints, locale, selectedTrendKeys)}
        />
      </button>
      {!isCollapsed ? (
        <div className="device-card-footer">
          <span>{t(locale, 'desiredInterval')}: {agent.desired_collection_interval_ms} ms</span>
          <span>{t(locale, 'desiredGpu')}: {agent.desired_enable_gpu ? t(locale, 'yes') : t(locale, 'no')}</span>
          {showOwner ? <span>{t(locale, 'owner')}: {agent.owner_user_id ?? t(locale, 'ownerUnknown')}</span> : null}
        </div>
      ) : null}
    </article>
  )
}

type SortableHeaderProps = {
  label: string
  sortKey: ProcessSortKey
  activeKey: ProcessSortKey
  direction: 'asc' | 'desc'
  onSort: (key: ProcessSortKey) => void
}

function SortableHeader({ label, sortKey, activeKey, direction, onSort }: SortableHeaderProps) {
  const isActive = sortKey === activeKey
  return (
    <th>
      <button type="button" className={isActive ? 'sort-button active' : 'sort-button'} onClick={() => onSort(sortKey)}>
        <span>{label}</span>
        <span>{isActive ? (direction === 'asc' ? '↑' : '↓') : '↕'}</span>
      </button>
    </th>
  )
}

type HistoryCardDefinition = {
  id: TrendKey
  key: string
  title: string
  accent: string
}

function buildHistoryCards(locale: Locale): HistoryCardDefinition[] {
  return [
    { id: 'cpu', key: 'cpu.usage_percent', title: t(locale, 'cpuHistory'), accent: '#73e0ce' },
    { id: 'memory', key: 'memory.used_bytes', title: t(locale, 'memoryHistory'), accent: '#bdf45f' },
    { id: 'gpu', key: 'gpus.0.utilization_percent', title: t(locale, 'gpuHistory'), accent: '#ffb86b' },
    { id: 'processes', key: 'processes.total_processes', title: t(locale, 'processHistory'), accent: '#7ab6ff' },
  ]
}

function buildChartSamples(points: MetricPayload[], metricPath: string, locale: Locale): ChartSample[] {
  return points
    .map((point) => {
      const value = latestMetricValue(point, metricPath)
      if (value == null) {
        return null
      }
      return {
        label: formatChartLabel(extractTimestamp(point), locale),
        value,
      }
    })
    .filter((sample): sample is ChartSample => sample !== null)
}

function latestMetricValue(payload: MetricPayload | null | undefined, metricPath: string): number | null {
  return getNumericMetric(payload, metricPath)
}

function formatMemorySummary(payload: MetricPayload | null | undefined, locale: Locale): string {
  const used = getNumericMetric(payload, 'memory.used_bytes')
  const total = getNumericMetric(payload, 'memory.total_bytes')
  if (used == null) {
    return '--'
  }
  return total != null ? `${formatBytes(used, locale)} / ${formatBytes(total, locale)}` : formatBytes(used, locale)
}

function formatGpuSummary(payload: MetricPayload | null | undefined, locale: Locale): string {
  const gpus = getGpuList(payload)
  if (!gpus.length) {
    return locale === 'zh-CN' ? '无' : 'None'
  }
  const busiest = [...gpus].sort((left, right) => (right.utilization_percent ?? 0) - (left.utilization_percent ?? 0))[0]
  return `GPU ${busiest.index} · ${formatPercent(busiest.utilization_percent, locale)}`
}

function formatGpuMemory(gpu: GpuMetric, locale: Locale): string {
  if (gpu.memory_used_bytes == null) {
    return '--'
  }
  if (gpu.memory_total_bytes == null) {
    return formatBytes(gpu.memory_used_bytes, locale)
  }
  return `${formatBytes(gpu.memory_used_bytes, locale)} / ${formatBytes(gpu.memory_total_bytes, locale)}`
}

function formatNetworkSummary(payload: MetricPayload | null | undefined, locale: Locale): string {
  const rx = getNumericMetric(payload, 'network.bytes_received')
  const tx = getNumericMetric(payload, 'network.bytes_sent')
  const iface = getTextMetric(payload, 'network.interface_name')
  const amount = `${formatBytes(rx, locale)} / ${formatBytes(tx, locale)}`
  return iface ? `${iface} · ${amount}` : amount
}

function buildCardTrendLines(points: AgentTrend['points'], locale: Locale, selectedTrendKeys: TrendKey[]): TrendLine[] {
  const maxProcessCount = Math.max(1, ...points.map((point) => point.process_count ?? 0))
  return [
    {
      key: 'cpu',
      label: t(locale, 'trendCpu'),
      color: '#7ee7d1',
      values: points.map((point) => clampPercent(point.cpu_usage_percent)),
    },
    {
      key: 'memory',
      label: t(locale, 'trendMemory'),
      color: '#d2f36d',
      values: points.map((point) => normalizeMemoryUsage(point.memory_used_bytes, point.memory_total_bytes)),
    },
    {
      key: 'gpu',
      label: t(locale, 'trendGpu'),
      color: '#ffb56f',
      values: points.map((point) => clampPercent(point.gpu_utilization_percent)),
    },
    {
      key: 'processes',
      label: t(locale, 'processCount'),
      color: '#7ab6ff',
      values: points.map((point) => normalizeProcessCount(point.process_count, maxProcessCount)),
    },
  ].filter((line) => selectedTrendKeys.includes(line.key as TrendKey))
}

function normalizeMemoryUsage(used: number | null, total: number | null): number | null {
  if (used == null || total == null || total <= 0) {
    return null
  }
  return clampPercent((used / total) * 100)
}

function clampPercent(value: number | null | undefined): number | null {
  if (value == null || !Number.isFinite(value)) {
    return null
  }
  return Math.min(Math.max(value, 0), 100)
}

function getGpuList(payload: MetricPayload | null | undefined): GpuMetric[] {
  return Array.isArray(payload?.gpus) ? (payload.gpus as GpuMetric[]) : []
}

function getProcessList(payload: MetricPayload | null | undefined): TopProcess[] {
  const allProcesses = getMetricValue(payload, 'processes.all_processes')
  if (Array.isArray(allProcesses)) {
    return allProcesses as TopProcess[]
  }
  return getTopProcesses(payload)
}

function getTopProcesses(payload: MetricPayload | null | undefined): TopProcess[] {
  const value = getMetricValue(payload, 'processes.top_processes')
  return Array.isArray(value) ? (value as TopProcess[]) : []
}

function sortProcesses(processes: TopProcess[], key: ProcessSortKey, direction: 'asc' | 'desc'): TopProcess[] {
  const multiplier = direction === 'asc' ? 1 : -1
  return [...processes].sort((left, right) => {
    const leftValue = processSortValue(left, key)
    const rightValue = processSortValue(right, key)
    const leftMissing = leftValue == null || leftValue === ''
    const rightMissing = rightValue == null || rightValue === ''
    if (leftMissing !== rightMissing) {
      return leftMissing ? 1 : -1
    }
    if (leftValue === rightValue) {
      return (left.pid - right.pid) * multiplier
    }
    if (typeof leftValue === 'string' || typeof rightValue === 'string') {
      return String(leftValue).localeCompare(String(rightValue), undefined, { numeric: true, sensitivity: 'base' }) * multiplier
    }
    return ((leftValue as number) - (rightValue as number)) * multiplier
  })
}

function processSortValue(process: TopProcess, key: ProcessSortKey): string | number | null {
  switch (key) {
    case 'pid':
      return process.pid
    case 'name':
      return process.name
    case 'user':
      return process.user ?? null
    case 'cpu_percent':
      return process.cpu_percent ?? null
    case 'memory_bytes':
      return process.memory_bytes ?? null
    case 'threads':
      return process.threads ?? null
    case 'state':
      return process.state ?? null
  }
}

function statusLabel(status: string, locale: Locale): string {
  if (status === 'online') {
    return t(locale, 'statusOnline')
  }
  if (status === 'offline') {
    return t(locale, 'statusOffline')
  }
  return t(locale, 'statusUnknown')
}

function normalizeProcessCount(value: number | null | undefined, maxValue: number): number | null {
  if (value == null || !Number.isFinite(value) || maxValue <= 0) {
    return null
  }
  return clampPercent((value / maxValue) * 100)
}

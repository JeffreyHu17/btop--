import type {
  AgentHistoryResponse,
  AgentSettings,
  AgentSettingsUpdate,
  AgentSummary,
  AgentTrend,
  ApiKey,
  ApiKeyCreateResponse,
  Dashboard,
  LoginRequest,
  TokenResponse,
  User,
} from './types'

export class ApiRequestError extends Error {
  constructor(
    message: string,
    public readonly status: number,
  ) {
    super(message)
    this.name = 'ApiRequestError'
  }
}

export class ApiClient {
  constructor(private readonly baseUrl = '/api/v1', private token: string | null = null) {}

  setToken(token: string | null) {
    this.token = token
  }

  async login(payload: LoginRequest): Promise<TokenResponse> {
    return this.request<TokenResponse>('/auth/login', {
      method: 'POST',
      body: JSON.stringify(payload),
    })
  }

  async me(): Promise<User> {
    return this.request<User>('/users/me')
  }

  async listAgents(): Promise<AgentSummary[]> {
    return this.request<AgentSummary[]>('/agents')
  }

  async getAgent(agentId: string): Promise<AgentSummary> {
    return this.request<AgentSummary>(`/agents/${agentId}`)
  }

  async getAgentHistory(agentId: string, minutes = 60): Promise<AgentHistoryResponse> {
    return this.request<AgentHistoryResponse>(`/agents/${agentId}/history?minutes=${minutes}`)
  }

  async listAgentTrends(minutes = 60, limit = 24): Promise<AgentTrend[]> {
    return this.request<AgentTrend[]>(`/agents/trends?minutes=${minutes}&limit=${limit}`)
  }

  async getAgentSettings(agentId: string): Promise<AgentSettings> {
    return this.request<AgentSettings>(`/agents/${agentId}/settings`)
  }

  async updateAgentSettings(agentId: string, payload: AgentSettingsUpdate): Promise<AgentSettings> {
    return this.request<AgentSettings>(`/agents/${agentId}/settings`, {
      method: 'PUT',
      body: JSON.stringify(payload),
    })
  }

  async deleteAgent(agentId: string): Promise<void> {
    await this.request<void>(`/agents/${agentId}`, {
      method: 'DELETE',
    })
  }

  async listApiKeys(): Promise<ApiKey[]> {
    return this.request<ApiKey[]>('/api-keys')
  }

  async createApiKey(label: string): Promise<ApiKeyCreateResponse> {
    return this.request<ApiKeyCreateResponse>('/api-keys', {
      method: 'POST',
      body: JSON.stringify({ label, scopes: 'metrics:write' }),
    })
  }

  async deleteApiKey(keyId: string): Promise<void> {
    await this.request<void>(`/api-keys/${keyId}`, {
      method: 'DELETE',
    })
  }

  async getDefaultDashboard(): Promise<Dashboard> {
    return this.request<Dashboard>('/dashboards/default')
  }

  async saveDefaultDashboard(layout: Dashboard['layout'], name = 'Default'): Promise<Dashboard> {
    return this.request<Dashboard>('/dashboards/default', {
      method: 'PUT',
      body: JSON.stringify({ name, layout }),
    })
  }

  private async request<T>(path: string, init?: RequestInit): Promise<T> {
    const headers = new Headers(init?.headers)
    headers.set('Content-Type', 'application/json')
    if (this.token) {
      headers.set('Authorization', `Bearer ${this.token}`)
    }

    const response = await fetch(`${this.baseUrl}${path}`, {
      ...init,
      headers,
    })

    if (!response.ok) {
      const contentType = response.headers.get('content-type') ?? ''
      if (contentType.includes('application/json')) {
        const data = (await response.json().catch(() => null)) as { detail?: unknown } | null
        const detail = typeof data?.detail === 'string' ? data.detail : null
        throw new ApiRequestError(detail ?? `Request failed with ${response.status}`, response.status)
      }

      const message = await response.text()
      throw new ApiRequestError(message || `Request failed with ${response.status}`, response.status)
    }

    if (response.status === 204) {
      return undefined as T
    }

    return (await response.json()) as T
  }
}

export type LoginRequest = {
  username: string
  password: string
}

export type User = {
  id: string
  username: string
  is_active: boolean
  is_superuser: boolean
  created_at: string
}

export type TokenResponse = {
  access_token: string
  token_type: 'bearer'
  user: User
}

export type ApiKey = {
  id: string
  label: string
  key_prefix: string
  scopes: string
  is_active: boolean
  created_at: string
  last_used_at: string | null
  revoked_at: string | null
}

export type ApiKeyCreateResponse = {
  api_key: string
  key: ApiKey
}

export type GpuMetric = {
  index: number
  utilization_percent?: number
  memory_used_bytes?: number
  memory_total_bytes?: number
  memory_utilization_percent?: number
  temperature_celsius?: number
  power_usage_watts?: number
  clock_speed_mhz?: number
  pcie_tx_kbps?: number
  pcie_rx_kbps?: number
  encoder_utilization_percent?: number
  decoder_utilization_percent?: number
}

export type TopProcess = {
  pid: number
  name: string
  user?: string
  memory_bytes?: number
  cpu_percent?: number
  threads?: number
  state?: string
}

export type MetricPayload = {
  timestamp?: string
  received_at?: string
  cpu?: {
    usage_percent?: number
    core_usage?: number[]
    temperature_celsius?: number
    frequency_mhz?: number
  }
  memory?: {
    total_bytes?: number
    used_bytes?: number
    available_bytes?: number
    cached_bytes?: number
    swap_total_bytes?: number
    swap_used_bytes?: number
  }
  network?: {
    bytes_sent?: number
    bytes_received?: number
    packets_sent?: number
    packets_received?: number
    interface_name?: string
  }
  gpus?: GpuMetric[]
  processes?: {
    total_processes?: number
    running_processes?: number
    sleeping_processes?: number
    load_average_1min?: number
    load_average_5min?: number
    load_average_15min?: number
    top_processes?: TopProcess[]
    all_processes?: TopProcess[]
  }
  [key: string]: unknown
}

export type AgentSummary = {
  id: string
  hostname: string
  display_name: string | null
  owner_user_id: string | null
  status: string
  desired_collection_interval_ms: number
  desired_enable_gpu: boolean
  last_seen_at: string | null
  latest_payload_at: string | null
  latest_payload: MetricPayload | null
}

export type AgentSettings = {
  agent_id: string
  hostname: string
  display_name: string | null
  desired_collection_interval_ms: number
  desired_enable_gpu: boolean
}

export type AgentSettingsUpdate = {
  display_name?: string | null
  desired_collection_interval_ms?: number
  desired_enable_gpu?: boolean
}

export type AgentHistoryResponse = {
  agent_id: string
  points: MetricPayload[]
}

export type AgentTrendPoint = {
  timestamp: string
  cpu_usage_percent: number | null
  memory_used_bytes: number | null
  memory_total_bytes: number | null
  gpu_utilization_percent: number | null
  process_count: number | null
  load_average_1min: number | null
}

export type AgentTrend = {
  agent_id: string
  points: AgentTrendPoint[]
}

export type DashboardLayout = {
  panels: Array<Record<string, unknown>>
  [key: string]: unknown
}

export type Dashboard = {
  id: string
  name: string
  is_default: boolean
  layout: DashboardLayout
  updated_at: string
}

<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'

type OperatorCodeType = 'opc' | 'op'
type NavSection = 'ue' | 'ue-list' | 'template' | 'user'

interface HealthInfo {
  ok: boolean
  mongoDb: string
  mongoCollection: string
  templateCollection: string
  defaultScscfUri: string
}

interface TemplateForm {
  id?: string
  name: string
  realm: string
  password: string
  ki: string
  operatorCodeType: OperatorCodeType
  operatorCode: string
  amf: string
  assignedScscf: string
  telPrefix: string
  telFromImsiDigits: string
  sqnStart: string
  sqnStep: string
}

interface TemplateItem {
  id: string
  name: string
  realm: string
  password: string
  ki: string
  operatorCodeType: OperatorCodeType
  operatorCode: string
  amf: string
  assignedScscf: string
  telPrefix: string
  telFromImsiDigits: number
  sqnStart: string
  sqnStep: string
  updatedAt: string
}

interface BatchForm {
  templateId: string
  startImsi: string
  ueCount: string
}

interface UeListItem {
  id: string
  imsi: string
}

interface UeDetail {
  id: string
  imsi: string
  tel: string
  realm: string
  identities: {
    impi: string
    canonicalImpu: string
    associatedImpus: string[]
    username: string
  }
  auth: {
    password: string
    ki: string
    operatorCodeType: string
    opc: string
    op: string
    sqn: string
    amf: string
  }
  serving: {
    assignedScscf: string
  }
}

interface UePagination {
  page: number
  pageSize: number
  total: number
  totalPages: number
}

interface UeEditForm {
  imsi: string
  tel: string
  realm: string
  password: string
  ki: string
  operatorCodeType: OperatorCodeType
  operatorCode: string
  sqn: string
  amf: string
  assignedScscf: string
}

const activeSection = ref<NavSection>('ue')
const sidebarCollapsed = ref(false)
const health = ref<HealthInfo | null>(null)
const templates = ref<TemplateItem[]>([])
const editingTemplateId = ref('')
const ueItems = ref<UeListItem[]>([])
const selectedUeId = ref('')
const ueDetail = ref<UeDetail | null>(null)
const ueModalVisible = ref(false)
const ueListLoading = ref(false)
const ueDetailLoading = ref(false)
const ueSearchInput = ref('')
const ueSearchQuery = ref('')
const ueEditMode = ref(false)
const ueSaving = ref(false)
const uePagination = reactive<UePagination>({
  page: 1,
  pageSize: 20,
  total: 0,
  totalPages: 1,
})
const uePageInput = ref('1')
const templateLoading = ref(false)
const batchLoading = ref(false)
const globalError = ref('')
const templateSuccess = ref('')
const batchSuccess = ref('')

const ueEditForm = reactive<UeEditForm>({
  imsi: '',
  tel: '',
  realm: '',
  password: '',
  ki: '',
  operatorCodeType: 'opc',
  operatorCode: '',
  sqn: '0',
  amf: '8000',
  assignedScscf: '',
})

const templateForm = reactive<TemplateForm>({
  name: '',
  realm: 'ims.local',
  password: '',
  ki: '',
  operatorCodeType: 'opc',
  operatorCode: '',
  amf: '8000',
  assignedScscf: '',
  telPrefix: '',
  telFromImsiDigits: '11',
  sqnStart: '0',
  sqnStep: '1',
})

const batchForm = reactive<BatchForm>({
  templateId: '',
  startImsi: '',
  ueCount: '1',
})

const navItems: Array<{ key: NavSection; label: string; icon: 'ue' | 'ue-list' | 'template' | 'user' }> = [
  { key: 'ue', label: '订阅UE信息', icon: 'ue' },
  { key: 'ue-list', label: '所有UE', icon: 'ue-list' },
  { key: 'template', label: '模板', icon: 'template' },
  { key: 'user', label: '登录用户', icon: 'user' },
]

const routeBySection: Record<NavSection, string> = {
  ue: '/ue-subscribe',
  'ue-list': '/ues',
  template: '/templates',
  user: '/users',
}

function resolveSectionFromPath(pathname: string): NavSection {
  const matched = (Object.entries(routeBySection) as Array<[NavSection, string]>).find(
    ([, routePath]) => routePath === pathname,
  )

  return matched?.[0] ?? 'ue'
}

function syncSectionFromLocation(replaceInvalidPath = false) {
  const section = resolveSectionFromPath(window.location.pathname)
  activeSection.value = section

  if (replaceInvalidPath && window.location.pathname !== routeBySection[section]) {
    window.history.replaceState({}, '', routeBySection[section])
  }
}

function navigateToSection(section: NavSection) {
  const targetPath = routeBySection[section]
  if (window.location.pathname !== targetPath) {
    window.history.pushState({}, '', targetPath)
  }
  activeSection.value = section
}

async function parseJsonResponse<T>(response: Response): Promise<T> {
  const rawText = await response.text()

  try {
    return JSON.parse(rawText) as T
  } catch {
    const preview = rawText.trim().slice(0, 120) || '空响应'
    throw new Error(`后端返回了非 JSON 响应：${preview}`)
  }
}

function resetTemplateForm() {
  templateForm.id = undefined
  templateForm.name = ''
  templateForm.realm = 'ims.local'
  templateForm.password = ''
  templateForm.ki = ''
  templateForm.operatorCodeType = 'opc'
  templateForm.operatorCode = ''
  templateForm.amf = '8000'
  templateForm.assignedScscf = health.value?.defaultScscfUri ?? ''
  templateForm.telPrefix = ''
  templateForm.telFromImsiDigits = '11'
  templateForm.sqnStart = '0'
  templateForm.sqnStep = '1'
}

function startNewTemplate() {
  editingTemplateId.value = ''
  resetTemplateForm()
}

function generateSequentialImsis(startImsi: string, ueCount: string) {
  const normalizedStart = startImsi.trim()
  const normalizedCount = ueCount.trim()

  if (!/^\d+$/.test(normalizedStart)) {
    return []
  }

  const count = Number.parseInt(normalizedCount, 10)
  if (!Number.isInteger(count) || count <= 0) {
    return []
  }

  const width = normalizedStart.length
  const start = BigInt(normalizedStart)

  return Array.from({ length: count }, (_, index) => {
    const nextValue = (start + BigInt(index)).toString()
    return nextValue.padStart(width, '0')
  })
}

const selectedTemplate = computed(() =>
  templates.value.find((item) => item.id === batchForm.templateId) ?? null,
)

const generatedImsis = computed(() => generateSequentialImsis(batchForm.startImsi, batchForm.ueCount))
const ueRangeLabel = computed(() => {
  if (uePagination.total === 0) {
    return '暂无 UE'
  }

  const start = (uePagination.page - 1) * uePagination.pageSize + 1
  const end = Math.min(start + ueItems.value.length - 1, uePagination.total)
  return `第 ${start}-${end} 条，共 ${uePagination.total} 条`
})

const loginUser = computed(() => ({
  name: 'admin',
  role: 'WebUI Operator',
  status: health.value ? '在线' : '离线',
}))

watch(
  () => health.value?.defaultScscfUri,
  (value) => {
    if (value && !templateForm.assignedScscf) {
      templateForm.assignedScscf = value
    }
  },
)

async function loadHealth() {
  const response = await fetch('/api/healthz')
  if (!response.ok) {
    throw new Error('后端服务不可用')
  }

  health.value = await parseJsonResponse<HealthInfo>(response)
  if (!templateForm.assignedScscf && health.value.defaultScscfUri) {
    templateForm.assignedScscf = health.value.defaultScscfUri
  }
}

async function loadTemplates() {
  const response = await fetch('/api/templates')
  if (!response.ok) {
    throw new Error('模板列表加载失败')
  }

  const payload = await parseJsonResponse<{ templates: TemplateItem[] }>(response)
  templates.value = payload.templates

  if (!batchForm.templateId && templates.value.length > 0) {
    batchForm.templateId = templates.value[0].id
  }

  if (editingTemplateId.value) {
    const currentTemplate = templates.value.find((item) => item.id === editingTemplateId.value)
    if (currentTemplate) {
      applyTemplate(currentTemplate, false)
    } else {
      startNewTemplate()
    }
  }
}

function applyTemplate(template: TemplateItem, showMessage = true) {
  editingTemplateId.value = template.id
  templateForm.id = template.id
  templateForm.name = template.name
  templateForm.realm = template.realm
  templateForm.password = template.password
  templateForm.ki = template.ki
  templateForm.operatorCodeType = template.operatorCodeType
  templateForm.operatorCode = template.operatorCode
  templateForm.amf = template.amf
  templateForm.assignedScscf = template.assignedScscf
  templateForm.telPrefix = template.telPrefix
  templateForm.telFromImsiDigits = String(template.telFromImsiDigits)
  templateForm.sqnStart = template.sqnStart
  templateForm.sqnStep = template.sqnStep
  batchForm.templateId = template.id
  if (showMessage) {
    templateSuccess.value = `已载入模板：${template.name}`
  }
  activeSection.value = 'template'
}

watch(editingTemplateId, (value) => {
  if (!value) {
    resetTemplateForm()
    return
  }

  const template = templates.value.find((item) => item.id === value)
  if (template) {
    applyTemplate(template, false)
  }
})

watch(activeSection, async (value) => {
  if (value === 'ue-list') {
    await loadUePage(1)
  }
})

async function loadUePage(page = uePagination.page) {
  ueListLoading.value = true
  globalError.value = ''

  try {
    const params = new URLSearchParams({
      page: String(page),
      pageSize: String(uePagination.pageSize),
    })
    if (ueSearchQuery.value) {
      params.set('q', ueSearchQuery.value)
    }

    const response = await fetch(`/api/ue?${params.toString()}`)
    const payload = await parseJsonResponse<{
      message?: string
      items?: UeListItem[]
      pagination?: UePagination
      search?: string
    }>(response)

    if (!response.ok) {
      throw new Error(payload.message ?? 'UE 列表加载失败')
    }

    ueItems.value = payload.items ?? []
    if (payload.pagination) {
      uePagination.page = payload.pagination.page
      uePagination.pageSize = payload.pagination.pageSize
      uePagination.total = payload.pagination.total
      uePagination.totalPages = payload.pagination.totalPages
      uePageInput.value = String(payload.pagination.page)
    }
    ueSearchInput.value = payload.search ?? ueSearchQuery.value

    if (ueItems.value.length === 0) {
      selectedUeId.value = ''
      ueDetail.value = null
      ueModalVisible.value = false
    } else if (!ueItems.value.some((item) => item.id === selectedUeId.value)) {
      selectedUeId.value = ''
      ueDetail.value = null
      ueModalVisible.value = false
    }
  } catch (error) {
    globalError.value = error instanceof Error ? error.message : 'UE 列表加载失败'
  } finally {
    ueListLoading.value = false
  }
}

async function goToUePage(page: number) {
  if (page < 1 || page > uePagination.totalPages || page === uePagination.page) {
    return
  }

  await loadUePage(page)
}

async function changeUePageSize(rawValue: string) {
  const pageSize = Number.parseInt(rawValue, 10)
  if (!Number.isInteger(pageSize) || pageSize <= 0) {
    return
  }

  uePagination.pageSize = pageSize
  uePageInput.value = '1'
  await loadUePage(1)
}

async function submitUeSearch() {
  ueSearchQuery.value = ueSearchInput.value.trim()
  uePageInput.value = '1'
  await loadUePage(1)
}

async function clearUeSearch() {
  if (!ueSearchQuery.value && !ueSearchInput.value) {
    return
  }

  ueSearchInput.value = ''
  ueSearchQuery.value = ''
  uePageInput.value = '1'
  await loadUePage(1)
}

async function submitUePageJump() {
  const targetPage = Number.parseInt(uePageInput.value, 10)
  if (!Number.isInteger(targetPage)) {
    uePageInput.value = String(uePagination.page)
    return
  }

  const normalizedPage = Math.min(Math.max(targetPage, 1), uePagination.totalPages)
  uePageInput.value = String(normalizedPage)
  await loadUePage(normalizedPage)
}

async function selectUe(id: string) {
  if (!id) {
    selectedUeId.value = ''
    ueDetail.value = null
    ueModalVisible.value = false
    return
  }

  ueDetailLoading.value = true
  globalError.value = ''
  ueModalVisible.value = true

  try {
    const response = await fetch(`/api/ue/${id}`)
    const payload = await parseJsonResponse<{
      message?: string
      item?: UeDetail
    }>(response)

    if (!response.ok) {
      throw new Error(payload.message ?? 'UE 详情加载失败')
    }

    selectedUeId.value = id
    ueDetail.value = payload.item ?? null
    syncUeEditForm()
    ueEditMode.value = false
  } catch (error) {
    globalError.value = error instanceof Error ? error.message : 'UE 详情加载失败'
  } finally {
    ueDetailLoading.value = false
  }
}

function closeUeModal() {
  ueModalVisible.value = false
  ueEditMode.value = false
}

function syncUeEditForm() {
  if (!ueDetail.value) {
    return
  }

  ueEditForm.imsi = ueDetail.value.imsi
  ueEditForm.tel = ueDetail.value.tel
  ueEditForm.realm = ueDetail.value.realm
  ueEditForm.password = ueDetail.value.auth.password
  ueEditForm.ki = ueDetail.value.auth.ki
  ueEditForm.operatorCodeType = ueDetail.value.auth.operatorCodeType === 'op' ? 'op' : 'opc'
  ueEditForm.operatorCode =
    ueEditForm.operatorCodeType === 'op' ? ueDetail.value.auth.op : ueDetail.value.auth.opc
  ueEditForm.sqn = ueDetail.value.auth.sqn || '0'
  ueEditForm.amf = ueDetail.value.auth.amf || '8000'
  ueEditForm.assignedScscf = ueDetail.value.serving.assignedScscf || ''
}

function startEditUe() {
  syncUeEditForm()
  ueEditMode.value = true
}

function cancelEditUe() {
  syncUeEditForm()
  ueEditMode.value = false
}

async function saveUe() {
  if (!ueDetail.value) {
    return
  }

  ueSaving.value = true
  globalError.value = ''

  try {
    const response = await fetch(`/api/ue/${ueDetail.value.id}`, {
      method: 'PUT',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        imsi: ueEditForm.imsi,
        tel: ueEditForm.tel,
        realm: ueEditForm.realm,
        password: ueEditForm.password,
        ki: ueEditForm.ki,
        operatorCodeType: ueEditForm.operatorCodeType,
        operatorCode: ueEditForm.operatorCode,
        sqn: ueEditForm.sqn,
        amf: ueEditForm.amf,
        assignedScscf: ueEditForm.assignedScscf,
      }),
    })

    const payload = await parseJsonResponse<{
      message?: string
      item?: UeDetail
    }>(response)

    if (!response.ok) {
      throw new Error(payload.message ?? 'UE 保存失败')
    }

    ueDetail.value = payload.item ?? null
    syncUeEditForm()
    ueEditMode.value = false
    batchSuccess.value = ''
    templateSuccess.value = ''
    await loadUePage(uePagination.page)
    if (ueDetail.value?.id) {
      await selectUe(ueDetail.value.id)
    }
  } catch (error) {
    globalError.value = error instanceof Error ? error.message : 'UE 保存失败'
  } finally {
    ueSaving.value = false
  }
}

async function saveTemplate() {
  templateLoading.value = true
  globalError.value = ''
  templateSuccess.value = ''

  try {
    const response = await fetch('/api/templates', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify(templateForm),
    })

    const payload = await parseJsonResponse<{ message?: string; template?: TemplateItem }>(response)
    if (!response.ok) {
      throw new Error(payload.message ?? '模板保存失败')
    }

    await loadTemplates()
    if (payload.template?.id) {
      editingTemplateId.value = payload.template.id
      batchForm.templateId = payload.template.id
      const savedTemplate = templates.value.find((item) => item.id === payload.template?.id)
      if (savedTemplate) {
        applyTemplate(savedTemplate, false)
      }
    }
    templateSuccess.value = payload.message ?? '模板已保存'
  } catch (error) {
    globalError.value = error instanceof Error ? error.message : '模板保存失败'
  } finally {
    templateLoading.value = false
  }
}

async function removeTemplate(templateId: string) {
  templateLoading.value = true
  globalError.value = ''
  templateSuccess.value = ''

  try {
    const response = await fetch(`/api/templates/${templateId}`, {
      method: 'DELETE',
    })
    const payload = await parseJsonResponse<{ message?: string }>(response)
    if (!response.ok) {
      throw new Error(payload.message ?? '模板删除失败')
    }

    if (batchForm.templateId === templateId) {
      batchForm.templateId = ''
    }
    await loadTemplates()
    if (editingTemplateId.value === templateId) {
      startNewTemplate()
    }
    templateSuccess.value = payload.message ?? '模板已删除'
  } catch (error) {
    globalError.value = error instanceof Error ? error.message : '模板删除失败'
  } finally {
    templateLoading.value = false
  }
}

async function generateBatch() {
  batchLoading.value = true
  globalError.value = ''
  batchSuccess.value = ''

  try {
    const response = await fetch('/api/ue/batch', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify(batchForm),
    })

    const payload = await parseJsonResponse<{
      message?: string
      insertedCount?: number
      templateName?: string
    }>(response)

    if (!response.ok) {
      throw new Error(payload.message ?? '批量生成失败')
    }

    batchSuccess.value =
      payload.message ??
      `已使用模板 ${payload.templateName ?? selectedTemplate.value?.name ?? ''} 生成 ${payload.insertedCount ?? 0} 个 UE`
  } catch (error) {
    globalError.value = error instanceof Error ? error.message : '批量生成失败'
  } finally {
    batchLoading.value = false
  }
}

onMounted(async () => {
  try {
    syncSectionFromLocation(true)
    await loadHealth()
    await loadTemplates()
  } catch (error) {
    globalError.value = error instanceof Error ? error.message : '初始化失败'
  }

  window.addEventListener('popstate', syncSectionFromLocation)
})

onBeforeUnmount(() => {
  window.removeEventListener('popstate', syncSectionFromLocation)
})
</script>

<template>
  <main class="page-shell" :class="{ collapsed: sidebarCollapsed }">
    <aside class="sidebar">
      <div class="sidebar-panel">
        <div class="sidebar-header">
          <div v-if="!sidebarCollapsed" class="brand-row">
            <div class="brand-mark">S</div>
            <h1>SimIMS</h1>
          </div>
          <button
            class="toggle-button"
            type="button"
            :aria-label="sidebarCollapsed ? '展开导航栏' : '收起导航栏'"
            @click="sidebarCollapsed = !sidebarCollapsed"
          >
            <span>{{ sidebarCollapsed ? '›' : '‹' }}</span>
          </button>
        </div>

        <nav class="nav-list">
          <button
            v-for="item in navItems"
            :key="item.key"
            class="nav-item"
            :class="{ active: activeSection === item.key, compact: sidebarCollapsed }"
            type="button"
            :title="sidebarCollapsed ? item.label : ''"
            @click="navigateToSection(item.key)"
          >
            <span class="nav-icon" aria-hidden="true">
              <svg v-if="item.icon === 'ue'" viewBox="0 0 24 24" fill="none">
                <path d="M6 7h12M6 12h12M6 17h8" />
              </svg>
              <svg v-else-if="item.icon === 'ue-list'" viewBox="0 0 24 24" fill="none">
                <path d="M4 7h16" />
                <path d="M4 12h16" />
                <path d="M4 17h16" />
                <path d="M7 5v14" />
              </svg>
              <svg v-else-if="item.icon === 'template'" viewBox="0 0 24 24" fill="none">
                <path d="M6 4h12v16H6z" />
                <path d="M9 8h6M9 12h6M9 16h4" />
              </svg>
              <svg v-else viewBox="0 0 24 24" fill="none">
                <path d="M12 12a4 4 0 1 0 0-8 4 4 0 0 0 0 8Z" />
                <path d="M5 20a7 7 0 0 1 14 0" />
              </svg>
            </span>
            <span v-if="!sidebarCollapsed" class="nav-label">
              {{ item.label }}
            </span>
          </button>
        </nav>

        <div class="sidebar-footer">
          <div
            class="status-pill"
            :class="{ ready: !!health, compact: sidebarCollapsed }"
            :title="health ? 'Mongo 已连接' : '等待后端'"
          >
            <span class="status-dot" aria-hidden="true"></span>
            <span v-if="!sidebarCollapsed">{{ health ? 'Mongo 已连接' : '等待后端' }}</span>
          </div>
        </div>
      </div>
    </aside>

    <section class="content-shell">
      <div v-if="globalError" class="banner error page-banner">{{ globalError }}</div>
      <div v-if="templateSuccess" class="banner success page-banner">{{ templateSuccess }}</div>
      <div v-if="batchSuccess" class="banner success page-banner">{{ batchSuccess }}</div>

      <section v-if="activeSection === 'ue'" class="content-panel">
        <header class="section-head">
          <div>
            <p class="card-kicker">订阅UE信息</p>
            <h2>按模板批量生成 UE</h2>
            <p class="section-copy">选择模板后输入起始 IMSI 和数量，系统自动按 IMSI 递增写入真实 MongoDB。</p>
          </div>
        </header>

        <article class="form-card">
          <form class="generator-form" @submit.prevent="generateBatch">
            <label>
              选择模板
              <select v-model="batchForm.templateId" required>
                <option value="" disabled>请选择模板</option>
                <option v-for="template in templates" :key="template.id" :value="template.id">
                  {{ template.name }} / {{ template.realm }}
                </option>
              </select>
            </label>

            <label>
              起始 IMSI
              <input v-model.trim="batchForm.startImsi" placeholder="460112024122023" required />
            </label>

            <label>
              UE 数量
              <input v-model.trim="batchForm.ueCount" placeholder="100" required />
            </label>

            <div class="action-row full-width">
              <div class="template-meta">
                <span>将从起始 IMSI 开始递增，生成 {{ generatedImsis.length }} 个 UE，重复 IMPI 会被 upsert 覆盖。</span>
              </div>
              <button class="submit-button" type="submit" :disabled="batchLoading || !batchForm.templateId">
                {{ batchLoading ? '生成中...' : '批量生成 UE' }}
              </button>
            </div>
          </form>
        </article>
      </section>

      <section v-if="activeSection === 'ue-list'" class="content-panel">
        <header class="section-head">
          <div>
            <p class="card-kicker">所有UE</p>
            <h2>UE 列表</h2>
            <p class="section-copy">按页查看当前 MongoDB 中的所有订阅 UE 信息。</p>
          </div>
          <button class="ghost-button" type="button" :disabled="ueListLoading" @click="loadUePage()">
            {{ ueListLoading ? '刷新中...' : '刷新列表' }}
          </button>
        </header>

        <article class="form-card ue-table-card">
          <div class="table-toolbar">
            <strong>{{ ueRangeLabel }}</strong>
            <span>每页 {{ uePagination.pageSize }} 条</span>
          </div>

          <form class="ue-search-bar" @submit.prevent="submitUeSearch">
            <label class="ue-search-input">
              <span>搜索 UE</span>
              <input v-model.trim="ueSearchInput" placeholder="输入 IMSI 或 IMPI 前缀" />
            </label>
            <button class="ghost-button" type="submit" :disabled="ueListLoading">搜索</button>
            <button class="mini-button" type="button" :disabled="ueListLoading" @click="clearUeSearch">
              清空
            </button>
          </form>

          <div v-if="ueListLoading" class="empty-state">正在加载 UE 列表...</div>
          <div v-else-if="ueItems.length === 0" class="empty-state">当前没有 UE 数据。</div>
          <div v-else class="ue-imsi-list">
            <button
              v-for="item in ueItems"
              :key="item.id"
              class="ue-imsi-item"
              :class="{ active: selectedUeId === item.id && ueModalVisible }"
              type="button"
              @click="selectUe(item.id)"
            >
              {{ item.imsi }}
            </button>
          </div>

          <div class="pagination-bar">
            <div class="pagination-group">
              <button
                class="mini-button"
                type="button"
                :disabled="ueListLoading || uePagination.page <= 1"
                @click="goToUePage(uePagination.page - 1)"
              >
                上一页
              </button>
              <span>第 {{ uePagination.page }} / {{ uePagination.totalPages }} 页</span>
              <button
                class="mini-button"
                type="button"
                :disabled="ueListLoading || uePagination.page >= uePagination.totalPages"
                @click="goToUePage(uePagination.page + 1)"
              >
                下一页
              </button>
            </div>

            <div class="pagination-group pagination-tools">
              <label class="pagination-size">
                <span>每页</span>
                <select
                  :value="String(uePagination.pageSize)"
                  :disabled="ueListLoading"
                  @change="changeUePageSize(($event.target as HTMLSelectElement).value)"
                >
                  <option value="10">10</option>
                  <option value="20">20</option>
                  <option value="50">50</option>
                  <option value="100">100</option>
                </select>
              </label>

              <form class="page-jump-form" @submit.prevent="submitUePageJump">
                <label class="page-jump-input">
                  <span>跳转到</span>
                  <input
                    v-model.trim="uePageInput"
                    inputmode="numeric"
                    :disabled="ueListLoading || uePagination.totalPages <= 1"
                  />
                </label>
                <button
                  class="ghost-button"
                  type="submit"
                  :disabled="ueListLoading || uePagination.totalPages <= 1"
                >
                  跳转
                </button>
              </form>
            </div>
          </div>
        </article>
      </section>

      <section v-if="activeSection === 'template'" class="content-panel">
        <header class="section-head">
          <div>
            <p class="card-kicker">模板</p>
            <h2>模板管理</h2>
            <p class="section-copy">维护 UE 生成模板，保存 realm、鉴权参数、TEL 规则和 SQN 规则。</p>
          </div>
        </header>

        <article class="form-card template-toolbar">
          <div class="template-toolbar-copy">
            <p class="card-kicker">模板列表</p>
            <h3>{{ templates.length }} 个模板</h3>
          </div>
          <div class="template-toolbar-actions">
            <label class="template-picker">
              <span>选择模板</span>
              <select v-model="editingTemplateId">
                <option value="">新增模板</option>
                <option v-for="template in templates" :key="template.id" :value="template.id">
                  {{ template.name }} / {{ template.realm }}
                </option>
              </select>
            </label>
            <button class="ghost-button" type="button" @click="startNewTemplate">新增模板</button>
            <button
              v-if="editingTemplateId"
              class="mini-button danger"
              type="button"
              @click="removeTemplate(editingTemplateId)"
            >
              删除当前模板
            </button>
          </div>
        </article>

        <article class="form-card">
          <form class="form-grid" @submit.prevent="saveTemplate">
            <label>
              模板名称
              <input v-model.trim="templateForm.name" placeholder="voNR-default" required />
            </label>

            <label>
              Realm
              <input
                v-model.trim="templateForm.realm"
                placeholder="ims.mnc011.mcc460.3gppnetwork.org"
                required
              />
            </label>

            <label>
              Password
              <input v-model.trim="templateForm.password" placeholder="模板默认密码" required />
            </label>

            <label>
              KI
              <input v-model.trim="templateForm.ki" placeholder="32 位十六进制" required />
            </label>

            <label>
              Operator Code Type
              <select v-model="templateForm.operatorCodeType">
                <option value="opc">OPC</option>
                <option value="op">OP</option>
              </select>
            </label>

            <label>
              {{ templateForm.operatorCodeType.toUpperCase() }}
              <input v-model.trim="templateForm.operatorCode" placeholder="32 位十六进制" required />
            </label>

            <label>
              AMF
              <input v-model.trim="templateForm.amf" placeholder="8000" required />
            </label>

            <label>
              Assigned S-CSCF
              <input
                v-model.trim="templateForm.assignedScscf"
                placeholder="sip:127.0.0.1:5062;transport=udp"
              />
            </label>

            <label>
              TEL 前缀
              <input v-model.trim="templateForm.telPrefix" placeholder="+86138" />
            </label>

            <label>
              从 IMSI 取末尾几位生成 TEL
              <input v-model.trim="templateForm.telFromImsiDigits" placeholder="11" />
            </label>

            <label>
              SQN 起始值
              <input v-model.trim="templateForm.sqnStart" placeholder="0" />
            </label>

            <label>
              SQN 步长
              <input v-model.trim="templateForm.sqnStep" placeholder="1" />
            </label>

            <div class="action-row full-width">
              <div class="template-meta">
                <span>模板保存到数据库后，可直接用于左侧“订阅UE信息”页面批量生成 UE。</span>
              </div>
              <button class="submit-button" type="submit" :disabled="templateLoading">
                {{ templateLoading ? '保存中...' : templateForm.id ? '更新模板' : '保存模板' }}
              </button>
            </div>
          </form>
        </article>
      </section>

      <section v-if="activeSection === 'user'" class="content-panel">
        <header class="section-head">
          <div>
            <p class="card-kicker">登录用户</p>
            <h2>当前会话信息</h2>
            <p class="section-copy">这个区域用于展示当前 WebUI 登录用户和基础系统连接状态。</p>
          </div>
        </header>

        <div class="user-grid">
          <article class="user-card">
            <h3>{{ loginUser.name }}</h3>
            <p class="user-role">{{ loginUser.role }}</p>
            <div class="user-status">
              <span>状态</span>
              <strong>{{ loginUser.status }}</strong>
            </div>
          </article>

          <article class="user-card">
            <h3>系统连接</h3>
            <div class="detail-list">
              <div><span>MongoDB</span><strong>{{ health?.mongoDb ?? '--' }}</strong></div>
              <div><span>订阅者集合</span><strong>{{ health?.mongoCollection ?? '--' }}</strong></div>
              <div><span>模板集合</span><strong>{{ health?.templateCollection ?? '--' }}</strong></div>
              <div><span>默认 S-CSCF</span><strong>{{ health?.defaultScscfUri ?? '--' }}</strong></div>
            </div>
          </article>
        </div>
      </section>
    </section>
  </main>

  <div v-if="ueModalVisible" class="modal-overlay" @click.self="closeUeModal">
    <section class="ue-modal">
      <div class="modal-head">
        <div>
          <p class="card-kicker">UE 详情</p>
          <h3>{{ ueDetail?.imsi ?? '正在加载...' }}</h3>
        </div>
        <div class="modal-actions">
          <button
            v-if="ueDetail && !ueEditMode"
            class="ghost-button"
            type="button"
            :disabled="ueDetailLoading"
            @click="startEditUe"
          >
            编辑
          </button>
          <button
            v-if="ueDetail && ueEditMode"
            class="ghost-button"
            type="button"
            :disabled="ueSaving"
            @click="cancelEditUe"
          >
            取消
          </button>
          <button
            v-if="ueDetail && ueEditMode"
            class="submit-button modal-save"
            type="button"
            :disabled="ueSaving"
            @click="saveUe"
          >
            {{ ueSaving ? '保存中...' : '保存' }}
          </button>
          <button class="modal-close" type="button" @click="closeUeModal">关闭</button>
        </div>
      </div>

      <div v-if="ueDetailLoading" class="empty-state">正在加载 UE 详情...</div>
      <div v-else-if="!ueDetail" class="empty-state">未获取到 UE 详情。</div>
      <div v-else-if="!ueEditMode" class="ue-detail-content">
        <div class="detail-block">
          <h3>基础信息</h3>
          <div class="detail-grid">
            <div><span>IMSI</span><strong>{{ ueDetail.imsi }}</strong></div>
            <div><span>TEL</span><strong>{{ ueDetail.tel || '--' }}</strong></div>
            <div><span>Realm</span><strong>{{ ueDetail.realm }}</strong></div>
            <div><span>S-CSCF</span><strong>{{ ueDetail.serving.assignedScscf || '--' }}</strong></div>
          </div>
        </div>

        <div class="detail-block">
          <h3>身份信息</h3>
          <div class="detail-grid">
            <div><span>IMPI</span><strong>{{ ueDetail.identities.impi }}</strong></div>
            <div><span>Canonical IMPU</span><strong>{{ ueDetail.identities.canonicalImpu }}</strong></div>
            <div><span>Username</span><strong>{{ ueDetail.identities.username }}</strong></div>
            <div>
              <span>Associated IMPUs</span>
              <strong>{{ ueDetail.identities.associatedImpus.join(' , ') || '--' }}</strong>
            </div>
          </div>
        </div>

        <div class="detail-block">
          <h3>鉴权信息</h3>
          <div class="detail-grid">
            <div><span>Password</span><strong>{{ ueDetail.auth.password }}</strong></div>
            <div><span>KI</span><strong>{{ ueDetail.auth.ki }}</strong></div>
            <div><span>Operator Code Type</span><strong>{{ ueDetail.auth.operatorCodeType }}</strong></div>
            <div><span>OPC</span><strong>{{ ueDetail.auth.opc || '--' }}</strong></div>
            <div><span>OP</span><strong>{{ ueDetail.auth.op || '--' }}</strong></div>
            <div><span>SQN</span><strong>{{ ueDetail.auth.sqn || '--' }}</strong></div>
            <div><span>AMF</span><strong>{{ ueDetail.auth.amf || '--' }}</strong></div>
          </div>
        </div>
      </div>
      <form v-else class="ue-edit-form" @submit.prevent="saveUe">
        <div class="detail-block">
          <h3>基础信息</h3>
          <div class="detail-grid">
            <label>
              <span>IMSI</span>
              <input v-model.trim="ueEditForm.imsi" required />
            </label>
            <label>
              <span>TEL</span>
              <input v-model.trim="ueEditForm.tel" />
            </label>
            <label>
              <span>Realm</span>
              <input v-model.trim="ueEditForm.realm" required />
            </label>
            <label>
              <span>S-CSCF</span>
              <input v-model.trim="ueEditForm.assignedScscf" />
            </label>
          </div>
        </div>

        <div class="detail-block">
          <h3>鉴权信息</h3>
          <div class="detail-grid">
            <label>
              <span>Password</span>
              <input v-model.trim="ueEditForm.password" required />
            </label>
            <label>
              <span>KI</span>
              <input v-model.trim="ueEditForm.ki" required />
            </label>
            <label>
              <span>Operator Code Type</span>
              <select v-model="ueEditForm.operatorCodeType">
                <option value="opc">OPC</option>
                <option value="op">OP</option>
              </select>
            </label>
            <label>
              <span>{{ ueEditForm.operatorCodeType.toUpperCase() }}</span>
              <input v-model.trim="ueEditForm.operatorCode" required />
            </label>
            <label>
              <span>SQN</span>
              <input v-model.trim="ueEditForm.sqn" />
            </label>
            <label>
              <span>AMF</span>
              <input v-model.trim="ueEditForm.amf" required />
            </label>
          </div>
        </div>
      </form>
    </section>
  </div>
</template>

<style scoped>
.page-shell {
  --sidebar-bg: linear-gradient(180deg, #111a23 0%, #172432 100%);
  --sidebar-border: rgba(255, 255, 255, 0.08);
  --sidebar-text: rgba(234, 241, 245, 0.94);
  --sidebar-muted: rgba(170, 186, 198, 0.78);
  --sidebar-hover: rgba(255, 255, 255, 0.06);
  --sidebar-active: linear-gradient(135deg, #1f8f63 0%, #1a6f51 100%);
  --surface-bg: rgba(255, 251, 244, 0.94);
  --surface-border: rgba(109, 101, 83, 0.16);
  max-width: 1480px;
  margin: 0 auto;
  padding: 20px 18px 40px;
  display: grid;
  grid-template-columns: 264px minmax(0, 1fr);
  gap: 18px;
}

.page-shell.collapsed {
  grid-template-columns: 88px minmax(0, 1fr);
}

.sidebar {
  min-height: calc(100vh - 40px);
}

.content-shell {
  display: grid;
  gap: 16px;
}

.sidebar-panel,
.content-panel,
.form-card,
.list-card,
.user-card {
  background: var(--surface-bg);
  border: 1px solid var(--surface-border);
  border-radius: 26px;
  box-shadow: 0 22px 80px rgba(42, 44, 36, 0.08);
  backdrop-filter: blur(10px);
}

.sidebar-panel,
.content-panel,
.form-card,
.list-card,
.user-card {
  padding: 24px;
}

.sidebar-panel {
  height: 100%;
  display: flex;
  flex-direction: column;
  gap: 18px;
  position: sticky;
  top: 20px;
  min-height: calc(100vh - 40px);
  padding: 18px 16px;
  background: var(--sidebar-bg);
  border-color: var(--sidebar-border);
  border-radius: 30px;
  box-shadow:
    0 24px 60px rgba(8, 14, 20, 0.34),
    inset 0 1px 0 rgba(255, 255, 255, 0.04);
  transition:
    width 0.22s ease,
    padding 0.22s ease,
    border-radius 0.22s ease,
    background 0.22s ease;
}

.sidebar-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  padding: 4px 4px 14px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
}

.brand-row {
  display: flex;
  align-items: center;
  gap: 12px;
  min-width: 0;
}

.brand-mark {
  width: 44px;
  height: 44px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border-radius: 16px;
  background: linear-gradient(135deg, #21a16f 0%, #0f6a4c 100%);
  color: white;
  font-weight: 800;
  font-size: 1.05rem;
  flex: 0 0 auto;
  box-shadow: 0 14px 28px rgba(15, 106, 76, 0.38);
}

.content-panel {
  display: grid;
  gap: 22px;
}

.panel-grid,
.user-grid {
  display: grid;
  grid-template-columns: 1.25fr 0.92fr;
  gap: 20px;
  align-items: start;
}

.card-kicker {
  margin: 0 0 10px;
  color: #137c5c;
  text-transform: uppercase;
  letter-spacing: 0.16em;
  font-size: 0.78rem;
  font-weight: 800;
}

h1,
h2,
h3,
p {
  margin-top: 0;
}

h1 {
  margin: 0;
  font-size: 1.2rem;
  line-height: 1;
  color: var(--sidebar-text);
  white-space: nowrap;
  letter-spacing: 0.01em;
}

.section-copy {
  margin-bottom: 0;
  color: #5d675f;
}

.nav-list {
  display: grid;
  gap: 8px;
  flex: 1 1 auto;
  align-content: start;
  padding-top: 6px;
}

.nav-item {
  width: 100%;
  text-align: left;
  border: 0;
  border-radius: 18px;
  padding: 13px 14px;
  background: transparent;
  color: var(--sidebar-muted);
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 12px;
  transition:
    background-color 0.18s ease,
    color 0.18s ease,
    transform 0.18s ease,
    box-shadow 0.18s ease;
}

.nav-item:hover {
  background: var(--sidebar-hover);
  color: var(--sidebar-text);
}

.nav-item.compact {
  justify-content: center;
  width: 56px;
  height: 56px;
  padding: 0;
  margin-inline: auto;
  border-radius: 18px;
}

.nav-item.active {
  background: var(--sidebar-active);
  color: white;
  box-shadow: 0 14px 30px rgba(15, 93, 69, 0.28);
}

.nav-icon {
  width: 26px;
  height: 26px;
  flex: 0 0 26px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
}

.nav-icon svg {
  width: 22px;
  height: 22px;
  stroke: currentColor;
  stroke-width: 1.8;
  stroke-linecap: round;
  stroke-linejoin: round;
}

.nav-label {
  font-size: 0.96rem;
  font-weight: 700;
  line-height: 1.2;
  color: currentColor;
}

.sidebar-footer {
  padding-top: 6px;
}

.toggle-button {
  width: 34px;
  height: 34px;
  border: 0;
  border-radius: 12px;
  background: rgba(255, 255, 255, 0.08);
  color: var(--sidebar-text);
  font-weight: 800;
  cursor: pointer;
  flex: 0 0 auto;
  transition:
    background-color 0.18s ease,
    color 0.18s ease;
}

.toggle-button:hover {
  background: rgba(255, 255, 255, 0.14);
}

.status-pill {
  display: inline-flex;
  width: 100%;
  border-radius: 18px;
  padding: 12px 14px;
  justify-content: center;
  align-items: center;
  gap: 8px;
  background: rgba(255, 255, 255, 0.06);
  border: 1px solid rgba(255, 255, 255, 0.08);
  color: var(--sidebar-muted);
  font-weight: 700;
}

.status-dot {
  width: 10px;
  height: 10px;
  border-radius: 999px;
  background: currentColor;
  opacity: 0.88;
  flex: 0 0 auto;
}

.status-pill.compact {
  width: 56px;
  height: 56px;
  min-height: 56px;
  padding: 0;
  border-radius: 18px;
  margin-inline: auto;
}

.status-pill.ready {
  background: rgba(33, 161, 111, 0.16);
  color: #d9fff0;
  border-color: rgba(33, 161, 111, 0.12);
}

.page-shell.collapsed .sidebar-panel {
  padding-left: 12px;
  padding-right: 12px;
}

.page-shell.collapsed .sidebar-header {
  flex-direction: column-reverse;
  align-items: center;
  padding-inline: 0;
}

.page-shell.collapsed .brand-row,
.page-shell.collapsed .sidebar-footer {
  justify-content: center;
}

.page-shell.collapsed .nav-list {
  justify-items: center;
}

.page-shell.collapsed .sidebar-footer {
  display: flex;
}

.page-shell.collapsed .toggle-button {
  width: 40px;
  height: 40px;
}

.detail-list span,
.user-status span {
  color: #6a746b;
  font-size: 0.88rem;
}

.detail-list strong {
  word-break: break-all;
}

.page-banner {
  margin: 0;
}

.banner {
  padding: 14px 16px;
  border-radius: 16px;
  font-weight: 700;
}

.banner.success {
  background: rgba(19, 124, 92, 0.11);
  color: #0f6e51;
}

.banner.error {
  background: rgba(184, 56, 73, 0.11);
  color: #a12a3b;
}

.section-head,
.card-head,
.template-item-head,
.template-actions {
  display: flex;
  justify-content: space-between;
  align-items: start;
  gap: 12px;
}

.section-head {
  padding-bottom: 4px;
}

.card-head h2 {
  margin-bottom: 0;
}

.template-toolbar {
  display: flex;
  justify-content: space-between;
  align-items: end;
  gap: 18px;
}

.template-toolbar-copy h3 {
  margin-bottom: 0;
}

.template-toolbar-actions {
  display: flex;
  align-items: end;
  gap: 12px;
  flex-wrap: wrap;
}

.template-picker {
  min-width: min(440px, 100%);
}

.template-picker span {
  font-size: 0.9rem;
}

.ue-table-card {
  display: grid;
  gap: 18px;
}

.ue-search-bar {
  display: flex;
  align-items: end;
  gap: 12px;
  flex-wrap: wrap;
}

.ue-search-input {
  min-width: min(420px, 100%);
}

.ue-search-input span {
  font-size: 0.9rem;
}

.ue-imsi-list {
  border: 1px solid rgba(109, 101, 83, 0.12);
  border-radius: 20px;
  background: rgba(255, 255, 255, 0.74);
}

.ue-imsi-list {
  display: grid;
  gap: 8px;
  padding: 12px;
  grid-template-columns: repeat(auto-fill, minmax(240px, 1fr));
  overflow: auto;
}

.ue-imsi-item {
  width: 100%;
  border: 0;
  border-radius: 16px;
  padding: 14px 16px;
  text-align: left;
  background: rgba(21, 32, 27, 0.04);
  color: #16211c;
  font-weight: 700;
  cursor: pointer;
  transition:
    background-color 0.18s ease,
    color 0.18s ease;
}

.ue-imsi-item:hover {
  background: rgba(19, 124, 92, 0.08);
}

.ue-imsi-item.active {
  background: linear-gradient(135deg, #137c5c 0%, #0f5d45 100%);
  color: white;
}

.modal-overlay {
  position: fixed;
  inset: 0;
  z-index: 50;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 24px;
  background: rgba(10, 18, 26, 0.52);
  backdrop-filter: blur(6px);
}

.ue-modal {
  width: min(960px, 100%);
  max-height: min(86vh, 920px);
  overflow: auto;
  background: rgba(255, 251, 244, 0.98);
  border: 1px solid rgba(109, 101, 83, 0.16);
  border-radius: 28px;
  box-shadow: 0 30px 80px rgba(8, 14, 20, 0.28);
  padding: 24px;
}

.modal-head {
  display: flex;
  align-items: start;
  justify-content: space-between;
  gap: 16px;
  margin-bottom: 18px;
}

.modal-actions {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
  justify-content: flex-end;
}

.modal-close {
  border: 0;
  border-radius: 999px;
  padding: 10px 16px;
  background: rgba(21, 32, 27, 0.08);
  color: #15201b;
  font-weight: 800;
  cursor: pointer;
}

.modal-save {
  min-width: 120px;
}

.ue-detail-content {
  display: grid;
  gap: 18px;
}

.ue-edit-form {
  display: grid;
  gap: 18px;
}

.detail-block {
  display: grid;
  gap: 14px;
}

.detail-block h3 {
  margin-bottom: 0;
}

.detail-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px;
}

.detail-grid div {
  display: grid;
  gap: 6px;
  padding: 14px 16px;
  border-radius: 16px;
  background: rgba(21, 32, 27, 0.04);
}

.detail-grid label {
  display: grid;
  gap: 8px;
  padding: 14px 16px;
  border-radius: 16px;
  background: rgba(21, 32, 27, 0.04);
}

.detail-grid span {
  color: #657067;
  font-size: 0.84rem;
}

.detail-grid strong {
  color: #16211c;
  word-break: break-all;
}

.table-toolbar,
.pagination-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.pagination-group {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}

.pagination-tools {
  justify-content: flex-end;
}

.pagination-size,
.page-jump-input {
  display: flex;
  align-items: center;
  gap: 8px;
}

.pagination-size span,
.page-jump-input span {
  color: #657067;
  font-size: 0.9rem;
  font-weight: 600;
}

.pagination-size select {
  width: 92px;
}

.page-jump-form {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

.page-jump-input input {
  width: 92px;
}

.table-toolbar span {
  color: #667066;
}

.table-wrap {
  overflow-x: auto;
  border: 1px solid rgba(109, 101, 83, 0.12);
  border-radius: 20px;
  background: rgba(255, 255, 255, 0.74);
}

.ue-table {
  width: 100%;
  border-collapse: collapse;
  min-width: 980px;
}

.ue-table th,
.ue-table td {
  padding: 14px 16px;
  text-align: left;
  border-bottom: 1px solid rgba(109, 101, 83, 0.1);
  vertical-align: top;
}

.ue-table th {
  color: #4f5f56;
  font-size: 0.84rem;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  background: rgba(19, 124, 92, 0.05);
}

.ue-table td {
  color: #16211c;
  word-break: break-all;
}

.ue-table tbody tr:last-child td {
  border-bottom: 0;
}

.form-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 16px;
}

.generator-form {
  display: grid;
  gap: 16px;
}

label {
  display: grid;
  gap: 8px;
  color: #526056;
  font-weight: 600;
}

input,
select {
  width: 100%;
  border: 1px solid rgba(94, 98, 80, 0.18);
  border-radius: 16px;
  padding: 13px 14px;
  background: rgba(255, 255, 255, 0.9);
  color: #15201b;
}

input:focus,
select:focus {
  outline: 2px solid rgba(19, 124, 92, 0.18);
  border-color: #137c5c;
}

.full-width {
  grid-column: 1 / -1;
}

.action-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 18px;
}

.submit-button,
.ghost-button,
.mini-button {
  border: 0;
  cursor: pointer;
  font-weight: 800;
}

.submit-button {
  border-radius: 999px;
  padding: 14px 22px;
  color: white;
  background: linear-gradient(135deg, #137c5c 0%, #0f5d45 100%);
  min-width: 180px;
}

.ghost-button {
  border-radius: 999px;
  padding: 10px 14px;
  background: rgba(19, 124, 92, 0.08);
  color: #137c5c;
}

.mini-button {
  border-radius: 999px;
  padding: 9px 12px;
  background: rgba(21, 32, 27, 0.06);
  color: #15201b;
}

.mini-button.danger {
  background: rgba(184, 56, 73, 0.1);
  color: #a12a3b;
}

.submit-button:disabled,
.ghost-button:disabled,
.mini-button:disabled {
  opacity: 0.65;
  cursor: wait;
}

.template-meta {
  color: #667066;
  font-size: 0.92rem;
}

.template-list {
  display: grid;
  gap: 14px;
}

.template-item {
  border: 1px solid rgba(109, 101, 83, 0.14);
  border-radius: 18px;
  padding: 16px;
  background: rgba(255, 255, 255, 0.72);
}

.template-item.active {
  border-color: rgba(19, 124, 92, 0.35);
  background: rgba(19, 124, 92, 0.06);
}

.template-item-head p {
  margin-bottom: 0;
  margin-top: 6px;
  color: #6a746b;
}

.template-item-head span {
  border-radius: 999px;
  padding: 6px 10px;
  background: rgba(19, 124, 92, 0.1);
  color: #137c5c;
  font-size: 0.8rem;
  font-weight: 800;
}

.template-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin: 14px 0;
}

.template-tags span {
  border-radius: 999px;
  padding: 6px 10px;
  background: rgba(21, 32, 27, 0.06);
  color: #415046;
  font-size: 0.82rem;
}

.detail-list {
  display: grid;
  gap: 10px;
}

.detail-list div,
.user-status {
  display: grid;
  gap: 6px;
}

.empty-state {
  color: #667066;
  padding: 20px 0;
}

.user-card h3 {
  margin-bottom: 8px;
}

.user-role {
  color: #5d675f;
  margin-bottom: 18px;
}

@media (max-width: 1160px) {
  .page-shell {
    grid-template-columns: 1fr;
  }

  .page-shell.collapsed {
    grid-template-columns: 1fr;
  }

  .sidebar {
    min-height: auto;
  }

  .sidebar-panel {
    position: static;
    min-height: auto;
  }
}

@media (max-width: 960px) {
  .panel-grid,
  .user-grid,
  .detail-grid {
    grid-template-columns: 1fr;
  }

  .sidebar {
    min-height: auto;
  }

  .sidebar-panel {
    height: auto;
    border-radius: 24px;
  }

  .ue-imsi-list {
    grid-template-columns: 1fr;
  }

  .page-shell.collapsed .sidebar-header {
    flex-direction: row;
    justify-content: space-between;
  }

  .page-shell.collapsed .nav-list {
    grid-template-columns: repeat(3, minmax(0, 1fr));
    justify-items: stretch;
  }

  .page-shell.collapsed .nav-item.compact {
    width: 100%;
  }
}

@media (max-width: 760px) {
  .form-grid {
    grid-template-columns: 1fr;
  }

  .section-head,
  .card-head,
  .template-item-head,
  .template-actions,
  .template-toolbar,
  .template-toolbar-actions,
  .ue-search-bar,
  .table-toolbar,
  .pagination-bar,
  .action-row {
    flex-direction: column;
    align-items: stretch;
  }

  .submit-button,
  .ghost-button,
  .mini-button,
  .modal-close,
  .modal-save {
    width: 100%;
  }

  .modal-actions {
    width: 100%;
    justify-content: stretch;
  }

  .page-shell,
  .page-shell.collapsed {
    grid-template-columns: 1fr;
    padding-inline: 14px;
  }

  .sidebar-panel,
  .page-shell.collapsed .sidebar-panel {
    padding: 16px 14px;
  }

  .modal-overlay {
    padding: 14px;
  }

  .ue-modal {
    padding: 18px;
    border-radius: 22px;
  }

  .nav-list,
  .page-shell.collapsed .nav-list {
    grid-template-columns: 1fr;
  }

  .nav-item,
  .page-shell.collapsed .nav-item.compact {
    width: 100%;
    height: auto;
    justify-content: flex-start;
    padding: 13px 14px;
    margin-inline: 0;
  }

  .page-shell.collapsed .sidebar-header {
    flex-direction: row;
  }

  .page-shell.collapsed .status-pill.compact {
    width: 100%;
    height: auto;
    min-height: 0;
    padding: 12px 14px;
  }
}
</style>

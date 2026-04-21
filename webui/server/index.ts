import express from 'express'
import { MongoClient, ObjectId } from 'mongodb'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import YAML from 'yaml'

type OperatorCodeType = 'opc' | 'op'

interface HssAdapterConfig {
  mongo_uri?: string
  mongo_db?: string
  mongo_collection?: string
  default_scscf_uri?: string
}

interface AppConfig {
  hss_adapter?: HssAdapterConfig
}

interface UePayload {
  imsi: string
  realm: string
  tel?: string
  password: string
  ki: string
  operatorCodeType: OperatorCodeType
  operatorCode: string
  sqn?: string
  amf?: string
  assignedScscf?: string
}

interface TemplatePayload {
  id?: string
  name: string
  realm: string
  password: string
  ki: string
  operatorCodeType: OperatorCodeType
  operatorCode: string
  amf?: string
  assignedScscf?: string
  telPrefix?: string
  telFromImsiDigits?: string
  sqnStart?: string
  sqnStep?: string
}

interface BatchPayload {
  templateId: string
  startImsi: string
  ueCount: string
}

interface SubscriberListItem {
  id: string
  imsi: string
}

interface SubscriberDetail {
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

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)
const webuiRoot = path.resolve(__dirname, '..')
const distDir = path.resolve(webuiRoot, 'dist')
const defaultConfigPath = path.resolve(webuiRoot, '..', 'config', 'ims.yaml')
const configPath = process.env.SIMIMS_CONFIG ?? process.argv[2] ?? defaultConfigPath
const port = Number(process.env.WEBUI_PORT ?? '18080')

function loadConfig(filePath: string) {
  const file = fs.readFileSync(filePath, 'utf8')
  const parsed = YAML.parse(file) as AppConfig
  const hss = parsed.hss_adapter ?? {}

  const mongoUri = hss.mongo_uri ?? 'mongodb://127.0.0.1:27017'
  const mongoDb = hss.mongo_db ?? 'simims'
  const mongoCollection = hss.mongo_collection ?? 'subscribers'
  const templateCollection = `${mongoCollection}_templates`
  const defaultScscfUri = hss.default_scscf_uri ?? 'sip:127.0.0.1:5062;transport=udp'

  return { mongoUri, mongoDb, mongoCollection, templateCollection, defaultScscfUri }
}

function ensureNonEmpty(value: unknown, field: string): string {
  if (typeof value !== 'string' || value.trim() === '') {
    throw new Error(`${field} 不能为空`)
  }

  return value.trim()
}

function ensureHex(value: string, length: number, field: string): string {
  const normalized = value.trim().toLowerCase()
  if (!new RegExp(`^[0-9a-f]{${length}}$`).test(normalized)) {
    throw new Error(`${field} 必须是 ${length} 位十六进制`)
  }

  return normalized
}

function parseSqn(rawValue?: string): number {
  const value = (rawValue ?? '0').trim()
  if (value === '') {
    return 0
  }

  const parsed = value.startsWith('0x') || value.startsWith('0X')
    ? Number.parseInt(value.slice(2), 16)
    : Number.parseInt(value, 10)

  if (!Number.isFinite(parsed) || parsed < 0) {
    throw new Error('SQN 格式不正确')
  }

  return parsed
}

function parsePositiveInteger(rawValue: unknown, fallbackValue: number, field: string) {
  if (rawValue === undefined || rawValue === null || String(rawValue).trim() === '') {
    return fallbackValue
  }

  const parsed = Number.parseInt(String(rawValue).trim(), 10)
  if (!Number.isInteger(parsed) || parsed < 0) {
    throw new Error(`${field} 必须是非负整数`)
  }

  return parsed
}

function escapeRegex(rawValue: string) {
  return rawValue.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
}

function generateSequentialImsis(startImsi: string, countRaw: string) {
  const normalizedStart = ensureNonEmpty(startImsi, '起始 IMSI')
  if (!/^\d+$/.test(normalizedStart)) {
    throw new Error('起始 IMSI 必须是纯数字')
  }

  const count = parsePositiveInteger(countRaw, 0, 'UE 数量')
  if (count <= 0) {
    throw new Error('UE 数量必须大于 0')
  }

  const width = normalizedStart.length
  const start = BigInt(normalizedStart)

  return Array.from({ length: count }, (_, index) => {
    const nextValue = (start + BigInt(index)).toString()
    return nextValue.padStart(width, '0')
  })
}

function buildAssociatedImpus(imsi: string, realm: string, tel?: string) {
  const associated = [`sip:${imsi}@${realm}`]

  if (tel && tel.trim() !== '') {
    associated.push(`sip:${tel.trim()}@${realm}`)
    associated.push(`tel:${tel.trim()}`)
  }

  return associated
}

function buildSubscriberDocument(payload: UePayload, defaultScscfUri: string) {
  const imsi = ensureNonEmpty(payload.imsi, 'IMSI')
  const realm = ensureNonEmpty(payload.realm, 'Realm')
  const password = ensureNonEmpty(payload.password, 'Password')
  const ki = ensureHex(ensureNonEmpty(payload.ki, 'KI'), 32, 'KI')
  const operatorCodeType = ensureNonEmpty(payload.operatorCodeType, 'OperatorCodeType') as OperatorCodeType
  const operatorCode = ensureHex(ensureNonEmpty(payload.operatorCode, 'OperatorCode'), 32, operatorCodeType.toUpperCase())
  const amf = ensureHex(payload.amf?.trim() || '8000', 4, 'AMF')
  const sqn = parseSqn(payload.sqn)
  const tel = payload.tel?.trim()
  const assignedScscf = payload.assignedScscf?.trim() || defaultScscfUri

  if (operatorCodeType !== 'opc' && operatorCodeType !== 'op') {
    throw new Error('operatorCodeType 只能是 opc 或 op')
  }

  return {
    imsi,
    ...(tel ? { tel } : {}),
    realm,
    identities: {
      impi: `${imsi}@${realm}`,
      canonical_impu: `sip:${imsi}@${realm}`,
      associated_impus: buildAssociatedImpus(imsi, realm, tel),
      username: imsi,
      realm,
    },
    auth: {
      password,
      ki,
      operator_code_type: operatorCodeType,
      opc: operatorCodeType === 'opc' ? operatorCode : '',
      op: operatorCodeType === 'op' ? operatorCode : '',
      sqn,
      amf,
    },
    profile: {
      ifcs: [],
    },
    serving: {
      assigned_scscf: assignedScscf,
    },
  }
}

function normalizeTemplate(payload: TemplatePayload, defaultScscfUri: string) {
  const name = ensureNonEmpty(payload.name, '模板名称')
  const realm = ensureNonEmpty(payload.realm, 'Realm')
  const password = ensureNonEmpty(payload.password, 'Password')
  const ki = ensureHex(ensureNonEmpty(payload.ki, 'KI'), 32, 'KI')
  const operatorCodeType = ensureNonEmpty(payload.operatorCodeType, 'OperatorCodeType') as OperatorCodeType
  const operatorCode = ensureHex(
    ensureNonEmpty(payload.operatorCode, 'OperatorCode'),
    32,
    operatorCodeType.toUpperCase(),
  )
  const amf = ensureHex(payload.amf?.trim() || '8000', 4, 'AMF')
  const assignedScscf = payload.assignedScscf?.trim() || defaultScscfUri
  const telPrefix = payload.telPrefix?.trim() || ''
  const telFromImsiDigits = parsePositiveInteger(payload.telFromImsiDigits, 11, 'telFromImsiDigits')
  const sqnStart = String(parseSqn(payload.sqnStart))
  const sqnStep = String(parsePositiveInteger(payload.sqnStep, 1, 'sqnStep'))

  if (operatorCodeType !== 'opc' && operatorCodeType !== 'op') {
    throw new Error('operatorCodeType 只能是 opc 或 op')
  }

  return {
    name,
    realm,
    password,
    ki,
    operatorCodeType,
    operatorCode,
    amf,
    assignedScscf,
    telPrefix,
    telFromImsiDigits,
    sqnStart,
    sqnStep,
    updatedAt: new Date().toISOString(),
  }
}

function mapTemplateDocument(document: Record<string, unknown>) {
  return {
    id: String(document._id),
    name: String(document.name ?? ''),
    realm: String(document.realm ?? ''),
    password: String(document.password ?? ''),
    ki: String(document.ki ?? ''),
    operatorCodeType: String(document.operatorCodeType ?? 'opc'),
    operatorCode: String(document.operatorCode ?? ''),
    amf: String(document.amf ?? '8000'),
    assignedScscf: String(document.assignedScscf ?? ''),
    telPrefix: String(document.telPrefix ?? ''),
    telFromImsiDigits: Number(document.telFromImsiDigits ?? 11),
    sqnStart: String(document.sqnStart ?? '0'),
    sqnStep: String(document.sqnStep ?? '1'),
    updatedAt: String(document.updatedAt ?? ''),
  }
}

function buildSubscriberDocumentFromTemplate(
  imsi: string,
  template: ReturnType<typeof normalizeTemplate>,
) {
  const tel =
    template.telPrefix && template.telFromImsiDigits > 0
      ? `${template.telPrefix}${imsi.slice(-template.telFromImsiDigits)}`
      : ''

  return buildSubscriberDocument(
    {
      imsi,
      realm: template.realm,
      tel,
      password: template.password,
      ki: template.ki,
      operatorCodeType: template.operatorCodeType,
      operatorCode: template.operatorCode,
      sqn: template.sqnStart,
      amf: template.amf,
      assignedScscf: template.assignedScscf,
    },
    template.assignedScscf,
  )
}

function mapSubscriberDocument(document: Record<string, unknown>): SubscriberListItem {
  return {
    id: String(document._id ?? ''),
    imsi: String(document.imsi ?? ''),
  }
}

function mapSubscriberDetail(document: Record<string, unknown>): SubscriberDetail {
  const identities = (document.identities as Record<string, unknown> | undefined) ?? {}
  const serving = (document.serving as Record<string, unknown> | undefined) ?? {}
  const auth = (document.auth as Record<string, unknown> | undefined) ?? {}
  const associatedImpus = Array.isArray(identities.associated_impus)
    ? identities.associated_impus.map((item) => String(item))
    : []

  return {
    id: String(document._id ?? ''),
    imsi: String(document.imsi ?? ''),
    tel: String(document.tel ?? ''),
    realm: String(document.realm ?? identities.realm ?? ''),
    identities: {
      impi: String(identities.impi ?? ''),
      canonicalImpu: String(identities.canonical_impu ?? ''),
      associatedImpus,
      username: String(identities.username ?? ''),
    },
    auth: {
      password: String(auth.password ?? ''),
      ki: String(auth.ki ?? ''),
      operatorCodeType: String(auth.operator_code_type ?? ''),
      opc: String(auth.opc ?? ''),
      op: String(auth.op ?? ''),
      sqn: String(auth.sqn ?? ''),
      amf: String(auth.amf ?? ''),
    },
    serving: {
      assignedScscf: String(serving.assigned_scscf ?? ''),
    },
  }
}

async function bootstrap() {
  const config = loadConfig(configPath)
  const mongo = new MongoClient(config.mongoUri)
  await mongo.connect()

  const collection = mongo.db(config.mongoDb).collection(config.mongoCollection)
  const templateCollection = mongo.db(config.mongoDb).collection(config.templateCollection)
  await Promise.all([
    collection.createIndex({ imsi: 1 }, { unique: true, name: 'simims_imsi_unique' }),
    collection.createIndex({ 'identities.impi': 1 }, { unique: true, name: 'simims_impi_unique' }),
    templateCollection.createIndex({ name: 1 }, { unique: true, name: 'simims_template_name_unique' }),
  ])
  const app = express()

  app.use(express.json())

  app.get('/api/healthz', (_req, res) => {
    res.json({
      ok: true,
      mongoDb: config.mongoDb,
      mongoCollection: config.mongoCollection,
      templateCollection: config.templateCollection,
      defaultScscfUri: config.defaultScscfUri,
    })
  })

  app.get('/api/templates', async (_req, res) => {
    try {
      const templateDocs = await templateCollection
        .find({})
        .sort({ updatedAt: -1, name: 1 })
        .toArray()

      res.json({
        ok: true,
        templates: templateDocs.map((item) => mapTemplateDocument(item as Record<string, unknown>)),
      })
    } catch (error) {
      res.status(500).json({
        ok: false,
        message: error instanceof Error ? error.message : '模板加载失败',
      })
    }
  })

  app.get('/api/ue', async (req, res) => {
    try {
      const page = Math.max(parsePositiveInteger(req.query.page, 1, 'page'), 1)
      const pageSize = Math.min(Math.max(parsePositiveInteger(req.query.pageSize, 20, 'pageSize'), 1), 100)
      const skip = (page - 1) * pageSize
      const search = typeof req.query.q === 'string' ? req.query.q.trim() : ''
      const filter =
        search === ''
          ? {}
          : {
              $or: [
                { imsi: { $regex: escapeRegex(search), $options: 'i' } },
                { 'identities.impi': { $regex: escapeRegex(search), $options: 'i' } },
              ],
            }

      const [total, subscribers] = await Promise.all([
        collection.countDocuments(filter),
        collection
          .find(
            filter,
            {
              projection: {
                imsi: 1,
                tel: 1,
                realm: 1,
                identities: 1,
                serving: 1,
                auth: 1,
              },
            },
          )
          .sort({ imsi: 1, _id: 1 })
          .skip(skip)
          .limit(pageSize)
          .toArray(),
      ])

      res.json({
        ok: true,
        items: subscribers.map((item) => mapSubscriberDocument(item as Record<string, unknown>)),
        pagination: {
          page,
          pageSize,
          total,
          totalPages: Math.max(Math.ceil(total / pageSize), 1),
        },
        search,
      })
    } catch (error) {
      res.status(400).json({
        ok: false,
        message: error instanceof Error ? error.message : 'UE 列表加载失败',
      })
    }
  })

  app.get('/api/ue/:id', async (req, res) => {
    try {
      const id = ensureNonEmpty(req.params.id, 'UE ID')
      const subscriber = await collection.findOne({ _id: new ObjectId(id) })

      if (!subscriber) {
        res.status(404).json({
          ok: false,
          message: 'UE 不存在',
        })
        return
      }

      res.json({
        ok: true,
        item: mapSubscriberDetail(subscriber as Record<string, unknown>),
      })
    } catch (error) {
      res.status(400).json({
        ok: false,
        message: error instanceof Error ? error.message : 'UE 详情加载失败',
      })
    }
  })

  app.put('/api/ue/:id', async (req, res) => {
    try {
      const id = ensureNonEmpty(req.params.id, 'UE ID')
      const objectId = new ObjectId(id)
      const existing = await collection.findOne({ _id: objectId })

      if (!existing) {
        res.status(404).json({
          ok: false,
          message: 'UE 不存在',
        })
        return
      }

      const document = buildSubscriberDocument(req.body as UePayload, config.defaultScscfUri)
      const duplicate = await collection.findOne({
        _id: { $ne: objectId },
        'identities.impi': document.identities.impi,
      })

      if (duplicate) {
        res.status(409).json({
          ok: false,
          message: '保存失败，IMPI 与其他 UE 冲突',
        })
        return
      }

      await collection.updateOne(
        { _id: objectId },
        {
          $set: document,
        },
      )

      const updated = await collection.findOne({ _id: objectId })
      if (!updated) {
        throw new Error('UE 保存后读取失败')
      }

      res.json({
        ok: true,
        message: 'UE 已保存',
        item: mapSubscriberDetail(updated as Record<string, unknown>),
      })
    } catch (error) {
      res.status(400).json({
        ok: false,
        message: error instanceof Error ? error.message : 'UE 保存失败',
      })
    }
  })

  app.post('/api/templates', async (req, res) => {
    try {
      const template = normalizeTemplate(req.body as TemplatePayload, config.defaultScscfUri)
      const id = typeof req.body.id === 'string' ? req.body.id.trim() : ''

      if (id) {
        await templateCollection.updateOne(
          { _id: new ObjectId(id) },
          { $set: template },
        )
      } else {
        await templateCollection.updateOne(
          { name: template.name },
          {
            $set: template,
            $setOnInsert: {
              createdAt: new Date().toISOString(),
            },
          },
          { upsert: true },
        )
      }

      const saved = await templateCollection.findOne({ name: template.name })
      if (!saved) {
        throw new Error('模板保存后读取失败')
      }

      res.json({
        ok: true,
        message: `模板 ${template.name} 已保存`,
        template: mapTemplateDocument(saved as Record<string, unknown>),
      })
    } catch (error) {
      res.status(400).json({
        ok: false,
        message: error instanceof Error ? error.message : '模板保存失败',
      })
    }
  })

  app.delete('/api/templates/:id', async (req, res) => {
    try {
      const id = ensureNonEmpty(req.params.id, '模板ID')
      await templateCollection.deleteOne({ _id: new ObjectId(id) })
      res.json({
        ok: true,
        message: '模板已删除',
      })
    } catch (error) {
      res.status(400).json({
        ok: false,
        message: error instanceof Error ? error.message : '模板删除失败',
      })
    }
  })

  app.post('/api/ue', async (req, res) => {
    try {
      const document = buildSubscriberDocument(req.body as UePayload, config.defaultScscfUri)
      await collection.replaceOne(
        { 'identities.impi': document.identities.impi },
        document,
        { upsert: true },
      )

      res.json({
        ok: true,
        message: 'UE 已写入 MongoDB',
        impi: document.identities.impi,
      })
    } catch (error) {
      res.status(400).json({
        ok: false,
        message: error instanceof Error ? error.message : '写入失败',
      })
    }
  })

  app.post('/api/ue/batch', async (req, res) => {
    try {
      const payload = req.body as BatchPayload
      const templateId = ensureNonEmpty(payload.templateId, '模板ID')
      const imsis = generateSequentialImsis(payload.startImsi, payload.ueCount)

      const templateDoc = await templateCollection.findOne({ _id: new ObjectId(templateId) })
      if (!templateDoc) {
        throw new Error('所选模板不存在')
      }

      const template = normalizeTemplate(
        {
          id: String(templateDoc._id),
          name: String(templateDoc.name ?? ''),
          realm: String(templateDoc.realm ?? ''),
          password: String(templateDoc.password ?? ''),
          ki: String(templateDoc.ki ?? ''),
          operatorCodeType: String(templateDoc.operatorCodeType ?? 'opc') as OperatorCodeType,
          operatorCode: String(templateDoc.operatorCode ?? ''),
          amf: String(templateDoc.amf ?? '8000'),
          assignedScscf: String(templateDoc.assignedScscf ?? ''),
          telPrefix: String(templateDoc.telPrefix ?? ''),
          telFromImsiDigits: String(templateDoc.telFromImsiDigits ?? '11'),
          sqnStart: String(templateDoc.sqnStart ?? '0'),
          sqnStep: String(templateDoc.sqnStep ?? '1'),
        },
        config.defaultScscfUri,
      )

      const sqnStart = parseSqn(template.sqnStart)
      const sqnStep = parsePositiveInteger(template.sqnStep, 1, 'sqnStep')

      const operations = imsis.map((imsi, index) => {
        const subscriber = buildSubscriberDocumentFromTemplate(imsi, {
          ...template,
          sqnStart: String(sqnStart + index * sqnStep),
        })

        return {
          replaceOne: {
            filter: { 'identities.impi': subscriber.identities.impi },
            replacement: subscriber,
            upsert: true,
          },
        }
      })

      const result = await collection.bulkWrite(operations, { ordered: false })

      res.json({
        ok: true,
        insertedCount: result.upsertedCount + result.modifiedCount + result.matchedCount,
        templateName: template.name,
        message: `已使用模板 ${template.name} 批量写入 ${imsis.length} 个 UE`,
      })
    } catch (error) {
      res.status(400).json({
        ok: false,
        message: error instanceof Error ? error.message : '批量生成失败',
      })
    }
  })

  if (fs.existsSync(distDir)) {
    app.use(express.static(distDir))
    app.get('*', (req, res, next) => {
      if (req.path.startsWith('/api/')) {
        next()
        return
      }

      res.sendFile(path.resolve(distDir, 'index.html'))
    })
  }

  app.listen(port, '0.0.0.0', () => {
    console.log(`SimIMS WebUI API listening on http://127.0.0.1:${port}`)
    console.log(`Using config: ${configPath}`)
    console.log(`MongoDB: ${config.mongoDb}.${config.mongoCollection}`)
  })
}

bootstrap().catch((error) => {
  console.error('Failed to start WebUI server')
  console.error(error)
  process.exit(1)
})

<template>
  <div class="app">
    <t-layout>
      <t-header class="header">
        <div class="logo">🤖 AI Agent</div>
        <div class="actions">
          <t-button theme="default" variant="outline" @click="exportSession" size="small">导出</t-button>
          <t-button theme="primary" variant="outline" @click="showImportModal = true" size="small">导入</t-button>
          <t-button theme="danger" variant="outline" @click="clearChat" size="small">清空</t-button>
        </div>
      </t-header>
      
      <t-layout>
        <t-aside width="200" class="sidebar">
          <t-menu :value="view" @change="v => view = v">
            <t-menu-item value="chat">💬 对话</t-menu-item>
            <t-menu-item value="memory">🧠 记忆</t-menu-item>
            <t-menu-item value="tools">🔧 工具</t-menu-item>
            <t-menu-item value="cron">⏰ 定时任务</t-menu-item>
            <t-menu-item value="skills">🎯 技能</t-menu-item>
          </t-menu>
        </t-aside>
        
        <t-content class="content">
          <!-- Chat View -->
          <div v-if="view === 'chat'" class="chat">
            <div class="messages" ref="msgBox">
              <div v-for="msg in messages" :key="msg.id" :class="['msg', msg.role]">
                <div class="avatar">{{ msg.role === 'user' ? '👤' : '🤖' }}</div>
                <div class="bubble" v-html="renderMd(msg.content)"></div>
              </div>
              <div v-if="loading" class="msg assistant">
                <div class="avatar">🤖</div>
                <div class="bubble"><t-loading text="思考中..." /></div>
              </div>
            </div>
            <div class="input-box">
              <t-textarea
                v-model="input"
                placeholder="输入消息... (Enter 发送)"
                :autosize="{ minRows: 2, maxRows: 4 }"
                @keydown.enter.exact.prevent="send"
              />
              <t-button theme="primary" @click="send" :loading="loading" :disabled="!input.trim()">
                发送
              </t-button>
            </div>
          </div>
          
          <!-- Memory View -->
          <div v-else-if="view === 'memory'" class="panel memory-panel">
            <div class="memory-header">
              <h2>🧠 记忆管理</h2>
              <div class="memory-stats">
                <t-tag theme="primary" variant="light">{{ messages.length }} 条消息</t-tag>
                <t-tag theme="success" variant="light">{{ sessions.length }} 个保存的会话</t-tag>
              </div>
            </div>
            
            <t-tabs v-model="memoryTab">
              <t-tab-panel value="current" label="当前会话">
                <div class="current-session">
                  <div class="session-actions">
                    <t-input v-model="sessionName" placeholder="会话名称" style="width: 200px" />
                    <t-button theme="primary" @click="saveSession" :disabled="messages.length === 0">
                      💾 保存会话
                    </t-button>
                  </div>
                  
                  <div class="message-list" v-if="messages.length > 0">
                    <div v-for="(msg, idx) in messages" :key="msg.id" class="memory-item">
                      <div class="memory-header-row">
                        <span class="role-tag" :class="msg.role">{{ msg.role === 'user' ? '👤 用户' : '🤖 助手' }}</span>
                        <span class="msg-index">#{{ idx + 1 }}</span>
                      </div>
                      <div class="memory-content" v-html="renderMd(msg.content)"></div>
                    </div>
                  </div>
                  <t-empty v-else description="暂无对话记录" />
                </div>
              </t-tab-panel>
              
              <t-tab-panel value="saved" label="保存的会话">
                <div class="saved-sessions">
                  <div class="search-box">
                    <t-input v-model="sessionSearch" placeholder="搜索会话..." clearable>
                      <template #prefix-icon>
                        <span>🔍</span>
                      </template>
                    </t-input>
                  </div>
                  
                  <div v-if="filteredSessions.length > 0" class="session-list">
                    <div v-for="session in filteredSessions" :key="session.id" class="session-card">
                      <div class="session-info">
                        <h3>{{ session.name }}</h3>
                        <div class="session-meta">
                          <span>{{ session.messageCount || 0 }} 条消息</span>
                          <span>{{ formatDate(session.savedAt) }}</span>
                        </div>
                      </div>
                      <div class="session-actions">
                        <t-button theme="primary" size="small" @click="loadSession(session)">
                          📂 加载
                        </t-button>
                        <t-button theme="danger" size="small" variant="outline" @click="deleteSession(session.id)">
                          🗑️ 删除
                        </t-button>
                      </div>
                    </div>
                  </div>
                  <t-empty v-else description="暂无保存的会话" />
                </div>
              </t-tab-panel>
            </t-tabs>
          </div>
          
          <!-- Tools View -->
          <div v-else-if="view === 'tools'" class="panel">
            <t-card title="可用工具" :bordered="false">
              <t-list>
                <t-list-item v-for="tool in tools" :key="tool">
                  <t-list-item-meta :title="tool" />
                </t-list-item>
              </t-list>
            </t-card>
          </div>
          
          <!-- Cron View -->
          <div v-else-if="view === 'cron'" class="panel">
            <div class="cron-header">
              <h2>⏰ 定时任务</h2>
              <div class="cron-actions">
                <t-button theme="primary" @click="showAddModal = true">+ 添加任务</t-button>
                <t-button :theme="cronRunning ? 'success' : 'default'" @click="toggleCron">
                  {{ cronRunning ? '⏹ 停止' : '▶ 启动' }}
                </t-button>
              </div>
            </div>
            
            <t-table :data="cronTasks" :columns="cronColumns" rowKey="id" :loading="cronLoading">
              <template #enabled="{ row }">
                <t-switch :value="row.enabled" @change="v => toggleTask(row.id, v)" />
              </template>
              <template #cron_expr="{ row }">
                <code class="cron-expr">{{ row.cron_expr }}</code>
              </template>
              <template #next_run="{ row }">
                <span class="time">{{ row.next_run || '-' }}</span>
              </template>
              <template #run_count="{ row }">
                <t-tag theme="primary" variant="light">{{ row.run_count }} 次</t-tag>
              </template>
              <template #op="{ row }">
                <t-button theme="danger" variant="text" @click="deleteTask(row.id)">删除</t-button>
              </template>
            </t-table>
            
            <!-- Add Task Modal -->
            <t-dialog v-model:visible="showAddModal" header="添加定时任务" @confirm="addTask">
              <t-form :data="newTask" labelAlign="right">
                <t-form-item label="名称" name="name">
                  <t-input v-model="newTask.name" placeholder="任务名称" />
                </t-form-item>
                <t-form-item label="Cron 表达式" name="cron">
                  <t-input v-model="newTask.cron" placeholder="例如: 0 8 * * * (每天8点)" />
                </t-form-item>
                <t-form-item label="执行命令" name="command">
                  <t-textarea v-model="newTask.command" placeholder="要发送给 AI 的指令" :autosize="{ minRows: 3 }" />
                </t-form-item>
              </t-form>
              <div class="cron-help">
                <p><strong>Cron 格式:</strong> minute hour day month weekday</p>
                <p>示例: <code>0 8 * * *</code> 每天8:00 | <code>30 9 * * 1-5</code> 工作日9:30</p>
              </div>
            </t-dialog>
          </div>
          
          <!-- Skills View -->
          <div v-else-if="view === 'skills'" class="panel">
            <t-card title="已加载技能" :bordered="false">
              <t-empty description="暂无技能" />
            </t-card>
          </div>
        </t-content>
      </t-layout>
    </t-layout>
    
    <!-- Import Modal -->
    <t-dialog v-model:visible="showImportModal" header="导入会话" @confirm="importSession">
      <t-upload
        v-model="importFiles"
        accept=".json"
        :autoUpload="false"
        theme="file"
        tips="选择 JSON 格式的会话文件"
      />
    </t-dialog>
  </div>
</template>

<script>
import { ref, onMounted, computed } from 'vue'
import { marked } from 'marked'
import axios from 'axios'
import { MessagePlugin } from 'tdesign-vue-next'

export default {
  setup() {
    const view = ref('chat')
    const messages = ref([])
    const input = ref('')
    const loading = ref(false)
    const tools = ref([])
    const msgBox = ref(null)
    
    // Memory state
    const memoryTab = ref('current')
    const sessions = ref([])
    const sessionName = ref('')
    const sessionSearch = ref('')
    const showImportModal = ref(false)
    const importFiles = ref([])
    
    // Cron state
    const cronTasks = ref([])
    const cronRunning = ref(false)
    const cronLoading = ref(false)
    const showAddModal = ref(false)
    const newTask = ref({ name: '', cron: '', command: '' })
    
    const cronColumns = [
      { colKey: 'enabled', title: '状态', width: 80 },
      { colKey: 'name', title: '名称', width: 150 },
      { colKey: 'cron_expr', title: 'Cron', width: 120 },
      { colKey: 'command', title: '命令', ellipsis: true },
      { colKey: 'next_run', title: '下次运行', width: 180 },
      { colKey: 'run_count', title: '运行次数', width: 100 },
      { colKey: 'op', title: '操作', width: 80 }
    ]
    
    // Computed: filtered sessions
    const filteredSessions = computed(() => {
      if (!sessionSearch.value.trim()) return sessions.value
      const query = sessionSearch.value.toLowerCase()
      return sessions.value.filter(s => 
        s.name.toLowerCase().includes(query)
      )
    })

    // ===== Memory Functions (Backend API) =====
    const loadSessions = async () => {
      try {
        const res = await axios.get('/api/sessions')
        sessions.value = (res.data.sessions || []).map(s => ({
          id: s.id,
          name: s.name,
          messages: [],  // Not loaded in list
          messageCount: s.message_count,
          savedAt: s.updated_at || s.created_at
        }))
      } catch (e) {
        console.error('Failed to load sessions:', e)
        sessions.value = []
      }
    }
    
    const saveSession = async () => {
      if (messages.value.length === 0) return
      // Auto-generate name from first user message
      let name = sessionName.value
      if (!name) {
        const firstUserMsg = messages.value.find(m => m.role === 'user')
        if (firstUserMsg) {
          // Use first 30 chars of first message as name
          name = firstUserMsg.content.slice(0, 30)
          if (firstUserMsg.content.length > 30) name += '...'
        } else {
          name = `会话 ${new Date().toLocaleString('zh-CN')}`
        }
      }
      try {
        const res = await axios.post('/api/sessions', { name })
        MessagePlugin.success('会话已保存')
        sessionName.value = ''
        loadSessions()  // Refresh list
      } catch (e) {
        MessagePlugin.error('保存失败: ' + (e.response?.data?.error || e.message))
      }
    }
    
    const loadSession = async (session) => {
      if (!confirm('加载会话将覆盖当前对话，确定继续？')) return
      try {
        // Load session into backend agent
        await axios.post(`/api/sessions/${session.id}/load`)
        // Fetch updated conversation
        const res = await axios.get('/api/conversation')
        messages.value = (res.data.messages || []).map(m => ({
          id: Date.now() + Math.random(),
          role: m.role,
          content: m.content
        }))
        view.value = 'chat'
        scrollToBottom()
        MessagePlugin.success('会话已加载')
      } catch (e) {
        MessagePlugin.error('加载失败: ' + (e.response?.data?.error || e.message))
      }
    }
    
    const deleteSession = async (id) => {
      if (!confirm('确定删除这个会话？')) return
      try {
        await axios.delete(`/api/sessions/${id}`)
        MessagePlugin.success('已删除')
        loadSessions()
      } catch (e) {
        MessagePlugin.error('删除失败')
      }
    }
    
    const exportSession = () => {
      const data = {
        version: 1,
        exportedAt: new Date().toISOString(),
        messages: messages.value
      }
      const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' })
      const url = URL.createObjectURL(blob)
      const a = document.createElement('a')
      a.href = url
      a.download = `ai-chat-${new Date().toISOString().slice(0, 10)}.json`
      a.click()
      URL.revokeObjectURL(url)
      MessagePlugin.success('已导出')
    }
    
    const importSession = async () => {
      if (importFiles.value.length === 0) {
        MessagePlugin.warning('请选择文件')
        return
      }
      try {
        const file = importFiles.value[0].raw
        const text = await file.text()
        const data = JSON.parse(text)
        if (data.messages && Array.isArray(data.messages)) {
          // Import by saving to backend
          await axios.post('/api/sessions', { 
            name: `导入 ${new Date().toLocaleString('zh-CN')}`,
            messages: data.messages 
          })
          MessagePlugin.success('已导入并保存')
          loadSessions()
        } else {
          throw new Error('无效的会话文件')
        }
      } catch (e) {
        MessagePlugin.error('导入失败: ' + e.message)
      }
      importFiles.value = []
    }
    
    const clearAllData = async () => {
      if (!confirm('确定清除所有保存的会话？此操作不可恢复！')) return
      try {
        // Delete all sessions from backend
        for (const s of sessions.value) {
          await axios.delete(`/api/sessions/${s.id}`)
        }
        sessions.value = []
        MessagePlugin.success('已清除所有数据')
      } catch (e) {
        MessagePlugin.error('清除失败')
      }
    }
    
    const formatDate = (iso) => {
      return new Date(iso).toLocaleString('zh-CN')
    }

    // ===== Chat Functions =====
    const send = async () => {
      if (!input.value.trim() || loading.value) return
      const content = input.value.trim()
      input.value = ''
      messages.value.push({ id: Date.now(), role: 'user', content })
      
      loading.value = true
      try {
        const res = await axios.post('/api/chat', { message: content })
        messages.value.push({ id: Date.now(), role: 'assistant', content: res.data.response })
      } catch (e) {
        messages.value.push({ id: Date.now(), role: 'assistant', content: '错误: ' + (e.message || '请求失败') })
      }
      loading.value = false
      scrollToBottom()
    }

    const clearChat = async () => {
      if (!confirm('确定清空当前对话？')) return
      await axios.delete('/api/conversation')
      messages.value = []
    }

    const renderMd = (text) => marked(text || '')
    
    const scrollToBottom = () => {
      setTimeout(() => {
        if (msgBox.value) msgBox.value.scrollTop = msgBox.value.scrollHeight
      }, 50)
    }

    const loadTools = async () => {
      try {
        const res = await axios.get('/api/tools')
        tools.value = res.data.tools || []
      } catch (e) {}
    }
    
    // ===== Cron Functions =====
    const loadCronTasks = async () => {
      cronLoading.value = true
      try {
        const res = await axios.get('/api/cron')
        cronTasks.value = res.data.tasks || []
        cronRunning.value = res.data.running || false
      } catch (e) {
        MessagePlugin.error('加载定时任务失败')
      }
      cronLoading.value = false
    }
    
    const toggleCron = async () => {
      try {
        if (cronRunning.value) {
          await axios.post('/api/cron/stop')
          cronRunning.value = false
          MessagePlugin.success('已停止调度器')
        } else {
          await axios.post('/api/cron/start')
          cronRunning.value = true
          MessagePlugin.success('已启动调度器')
        }
      } catch (e) {
        MessagePlugin.error('操作失败')
      }
    }
    
    const addTask = async () => {
      if (!newTask.value.name || !newTask.value.cron || !newTask.value.command) {
        MessagePlugin.warning('请填写完整信息')
        return
      }
      try {
        await axios.post('/api/cron', newTask.value)
        MessagePlugin.success('添加成功')
        showAddModal.value = false
        newTask.value = { name: '', cron: '', command: '' }
        loadCronTasks()
      } catch (e) {
        MessagePlugin.error('添加失败')
      }
    }
    
    const toggleTask = async (id, enabled) => {
      try {
        await axios.post(`/api/cron/${id}/toggle`, { enabled })
        loadCronTasks()
      } catch (e) {
        MessagePlugin.error('操作失败')
      }
    }
    
    const deleteTask = async (id) => {
      if (!confirm('确定删除此任务？')) return
      try {
        await axios.delete(`/api/cron/${id}`)
        MessagePlugin.success('删除成功')
        loadCronTasks()
      } catch (e) {
        MessagePlugin.error('删除失败')
      }
    }
    
    // Load current conversation from backend
    const loadCurrentConversation = async () => {
      try {
        const res = await axios.get('/api/conversation')
        messages.value = (res.data.messages || []).map(m => ({
          id: Date.now() + Math.random(),
          role: m.role,
          content: m.content
        }))
      } catch (e) {
        console.error('Failed to load conversation:', e)
      }
    }

    onMounted(() => {
      loadSessions()
      loadCurrentConversation()
      loadTools()
      loadCronTasks()
    })

    return { 
      view, messages, input, loading, tools, msgBox, 
      send, clearChat, renderMd,
      // Memory
      memoryTab, sessions, sessionSearch, filteredSessions, sessionName, 
      showImportModal, importFiles,
      saveSession, loadSession, deleteSession, exportSession, importSession,
      clearAllData, formatDate,
      // Cron
      cronTasks, cronRunning, cronLoading, showAddModal, newTask, cronColumns,
      loadCronTasks, toggleCron, addTask, toggleTask, deleteTask
    }
  }
}
</script>

<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: -apple-system, sans-serif; }

.app { height: 100vh; }
.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 0 24px;
  background: #fff;
  border-bottom: 1px solid #eee;
}
.logo { font-size: 18px; font-weight: 600; }
.actions { display: flex; gap: 8px; }
.sidebar { background: #fff; border-right: 1px solid #eee; }
.content { background: #f5f5f5; padding: 24px; }

.chat { display: flex; flex-direction: column; height: calc(100vh - 120px); background: #fff; border-radius: 8px; }
.messages { flex: 1; overflow-y: auto; padding: 24px; }
.msg { display: flex; gap: 12px; margin-bottom: 16px; }
.msg.user { flex-direction: row-reverse; }
.avatar { font-size: 24px; }
.bubble {
  max-width: 70%;
  padding: 12px 16px;
  border-radius: 12px;
  background: #f5f5f5;
}
.msg.user .bubble { background: #0052d9; color: #fff; }
.input-box {
  display: flex;
  gap: 12px;
  padding: 16px;
  border-top: 1px solid #eee;
}
.input-box .t-textarea { flex: 1; }

.panel { background: #fff; border-radius: 8px; padding: 24px; }

/* Memory Panel */
.memory-panel { min-height: calc(100vh - 120px); }
.memory-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}
.memory-header h2 { margin: 0; }
.memory-stats { display: flex; gap: 8px; }

.current-session { padding: 16px 0; }
.session-actions { display: flex; gap: 12px; margin-bottom: 16px; }
.message-list { max-height: 500px; overflow-y: auto; }
.memory-item {
  padding: 12px;
  border: 1px solid #eee;
  border-radius: 8px;
  margin-bottom: 12px;
}
.memory-header-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}
.role-tag {
  font-size: 12px;
  padding: 2px 8px;
  border-radius: 4px;
  background: #f5f5f5;
}
.role-tag.user { background: #e3f2ff; color: #0052d9; }
.role-tag.assistant { background: #e8f5e9; color: #2e7d32; }
.msg-index { font-size: 12px; color: #999; }
.memory-content {
  font-size: 14px;
  line-height: 1.6;
  color: #333;
}
.memory-content pre { background: #f5f5f5; padding: 8px; border-radius: 4px; overflow-x: auto; }
.memory-content code { background: #f5f5f5; padding: 2px 4px; border-radius: 2px; }

.saved-sessions { padding: 16px 0; }
.search-box { margin-bottom: 16px; }
.session-list { display: flex; flex-direction: column; gap: 12px; }
.session-card {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px;
  border: 1px solid #eee;
  border-radius: 8px;
  transition: all 0.2s;
}
.session-card:hover { border-color: #0052d9; box-shadow: 0 2px 8px rgba(0,82,217,0.1); }
.session-info h3 { margin: 0 0 8px 0; font-size: 16px; }
.session-meta { display: flex; gap: 16px; font-size: 12px; color: #999; }
.session-card .session-actions { display: flex; gap: 8px; margin: 0; }

/* Cron */
.cron-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}
.cron-header h2 { margin: 0; }
.cron-actions { display: flex; gap: 8px; }
.cron-expr { 
  background: #f5f5f5; 
  padding: 2px 6px; 
  border-radius: 4px; 
  font-size: 12px;
}
.time { font-size: 12px; color: #666; }
.cron-help {
  margin-top: 16px;
  padding: 12px;
  background: #f5f5f5;
  border-radius: 4px;
  font-size: 12px;
}
.cron-help p { margin: 4px 0; }
.cron-help code { background: #e0e0e0; padding: 2px 4px; border-radius: 2px; }
</style>

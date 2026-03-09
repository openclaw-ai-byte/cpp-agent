<template>
  <div class="app">
    <t-layout>
      <t-header class="header">
        <div class="logo">🤖 AI Agent</div>
        <div class="actions">
          <t-button theme="danger" variant="outline" @click="clearChat">清空对话</t-button>
        </div>
      </t-header>
      
      <t-layout>
        <t-aside width="200" class="sidebar">
          <t-menu :value="view" @change="v => view = v">
            <t-menu-item value="chat">💬 对话</t-menu-item>
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
  </div>
</template>

<script>
import { ref, onMounted } from 'vue'
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
      await axios.delete('/api/conversation')
      messages.value = []
    }

    const renderMd = (text) => marked(text || '')
    
    const scrollToBottom = () => {
      if (msgBox.value) msgBox.value.scrollTop = msgBox.value.scrollHeight
    }

    const loadTools = async () => {
      try {
        const res = await axios.get('/api/tools')
        tools.value = res.data.tools || []
      } catch (e) {}
    }
    
    // Cron functions
    const loadCronTasks = async () => {
      cronLoading.value = true
      try {
        const res = await axios.get('/api/cron')
        cronTasks.value = res.data.tasks || []
      } catch (e) {
        MessagePlugin.error('加载定时任务失败')
      }
      cronLoading.value = false
    }
    
    const checkCronStatus = async () => {
      try {
        const res = await axios.get('/api/cron')
        cronRunning.value = true // 如果能获取任务说明在运行
      } catch (e) {}
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
      try {
        await axios.delete(`/api/cron/${id}`)
        MessagePlugin.success('删除成功')
        loadCronTasks()
      } catch (e) {
        MessagePlugin.error('删除失败')
      }
    }

    onMounted(() => {
      loadTools()
      loadCronTasks()
    })

    return { 
      view, messages, input, loading, tools, msgBox, 
      send, clearChat, renderMd,
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

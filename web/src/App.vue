<template>
  <div class="app">
    <t-layout>
      <t-header class="header">
        <div class="logo">🤖 AI Agent</div>
        <div class="actions">
          <t-button theme="danger" variant="outline" @click="clearChat">清空</t-button>
        </div>
      </t-header>
      
      <t-layout>
        <t-aside width="200" class="sidebar">
          <t-menu :value="view" @change="v => view = v">
            <t-menu-item value="chat">💬 对话</t-menu-item>
            <t-menu-item value="tools">🔧 工具</t-menu-item>
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
                <t-list-item v-for="tool in tools" :key="tool.name">
                  <t-list-item-meta :title="tool.name" :description="tool.description" />
                </t-list-item>
              </t-list>
            </t-card>
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
import { ref, nextTick } from 'vue'
import { marked } from 'marked'
import axios from 'axios'

export default {
  setup() {
    const view = ref('chat')
    const messages = ref([])
    const input = ref('')
    const loading = ref(false)
    const tools = ref([])
    const msgBox = ref(null)

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
      nextTick(() => {
        if (msgBox.value) msgBox.value.scrollTop = msgBox.value.scrollHeight
      })
    }

    const loadTools = async () => {
      try {
        const res = await axios.get('/api/tools')
        tools.value = res.data.tools || []
      } catch (e) {}
    }

    loadTools()

    return { view, messages, input, loading, tools, msgBox, send, clearChat, renderMd }
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
</style>

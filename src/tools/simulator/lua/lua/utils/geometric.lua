--region utils.geometric.lua
--Author : OWenT
--Date   : 2014/11/08
--计算集合（移植自很久很久以前ACM时期的计算几何模板）
--@see http://www.owent.net/?p=11
--@see https://github.com/owt5008137/ACM-Personal-Code
--@note 不完全移植，用一点，移植一点，哇哈哈哈哈

local class = require ('utils.class')

local geometric = class.register('utils.geometric', class.singleton)

-- 该数字来自std::numeric_limits<double>::epsilon()
math.epsilon = math.epsilon or 0.0000001192092896 

-- ====================== 点 ====================== 
geometric.point = class.register('utils.geometric.point')
do
    local pt_new = geometric.point.new
    geometric.point.new = function(...)
        local ret = pt_new(...)
        ret.x = ret.x or 0
        ret.y = ret.y or 0
        return ret
    end
end

-- 计算距离的平方(支持直接传table)
function geometric.point.dis2(self, other)
    return (self.x  - other.x) * (self.x  - other.x) + (self.y  - other.y) * (self.y  - other.y)
end

-- 计算距离(支持直接传table)
function geometric.point.dis(self, other)
    return math.sqrt(geometric.point.dis2(self, other))
end

-- 差乘，相对于原点(ori)
-- 即由原点(ori)和这两个点(self, other)组成的平行四边形面积
function geometric.point:xmult(other, ori)
    if ori then
        return (self.x - ori.x) * (other.y - ori.y) - (other.x - ori.x) * (self.y - ori.y);
    else
        return self.x * other.y - self.y * other.x;
    end
end

-- 相等判断
geometric.point.equal = function(left, right)
    return math.abs(left.x - right.x) <= math.epsilon and math.abs(left.y - right.y) <= math.epsilon
end

-- 重载*操作符(相对于原点差乘)
geometric.point.__mul = function(left, right)
    return left:xmult(right)
end

-- ---------------------- 点 ----------------------

-- ====================== 向量 ====================== 
geometric.vector = class.register('utils.geometric.vector')
do
    local vec_new = geometric.vector.new
    geometric.vector.new = function(...)
        local ret = vec_new(...)
        ret.s = ret.s or geometric.point.new()
        ret.e = ret.e or geometric.point.new()
        return ret
    end
end

-- 获取两点式
function geometric.vector:points()
    return ret.s, ret.e
end

-- 获取一般式(ax + by + c = 0)
function geometric.vector:pton()
    local a = s.y - e.y;
    local b = e.x - s.x;
    local c = s.x * e.y - s.y * e.x;
    return a, b, c;
end

-- 向量与向量或点的差乘
function geometric.vector:xmult(other)
    -- 视为点
    if other.x and other.y then
        return (other.y - self.s.y) * (self.e.x - self.s.x) - (other.x - self.s.x) * (self.e.y - self.s.y);
    else
    -- 视为向量
        return (self.e.x - self.s.x) * (other.e.y - other.s.y) - (self.e.y - self.s.y) * (other.e.x - other.s.x);
    end
end

-- ===== 向量和点 =====
-- 关于直线对称的点
function geometric.vector:mirror(other)
    local ret = geometric.point.new()
    local a, b, c = self:pton()
    local d = a * a + b * b;
    ret.x = (b * b * other.x - a * a * other.x - 2 * a * b * other.y - 2 * a * c) / d;
    ret.y = (a * a * other.y - b * b * other.y - 2 * a * b * other.x - 2 * b * c) / d;
    return ret;
end

-- ===== 向量和直线 =====
-- 两直线交点
function geometric.vector:crossLPt(other)
    local ret = self.s:clone()
    local t = ((self.s.x - other.s.x) * (other.s.y - other.e.y) - (self.s.y - other.s.y) * (other.s.x - other.e.x)) / ((self.s.x - self.e.x) * (other.s.y - other.e.y) - (self.s.y - self.e.y) * (other.s.x - other.e.x));
    ret.x = ret.x + (self.e.x - self.s.x) * t;
    ret.y = ret.y + (self.e.y - self.s.y) * t;
    return ret;
end
-- ===== 向量和线段 =====

-- ---------------------- 向量 ----------------------

-- ====================== 多边形 ====================== 
-- ===== 面积 =====
-- ===== 重心 =====
-- ===== 点在凸多边形内 =====
-- ===== 点在任意多边形内[射线法] =====
-- ===== 凸多边形呗直线切割 =====
-- ===== 凸包（Graham扫描法，复杂度O(nlg(n)),结果为逆时针） =====
-- ===== 凸包直径的平方（旋转卡壳） =====
-- ===== 凸包最短距离（旋转卡壳） =====
-- ===== 半平面交 =====
-- ---------------------- 多边形 ----------------------

-- ====================== 圆 ====================== 
-- ---------------------- 圆 ----------------------

-- ====================== 三角形 ====================== 
geometric.triangle = class.register('utils.geometric.triangle')
do
    local tri_new = geometric.triangle.new
    geometric.triangle.new = function(...)
        local ret = tri_new(...)
        ret.a = ret.a or geometric.point.new()
        ret.b = ret.b or geometric.point.new()
        ret.c = ret.c or geometric.point.new()
        return ret
    end
end
-- 三角形面积(海伦公式)
function geometric.triangle:area()
    return math.abs(a:xmult(b, c)) / 2
end

-- ---------------------- 三角形 ----------------------

return geometric
--endregion

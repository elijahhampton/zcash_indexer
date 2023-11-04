SELECT 
    start.height + 1 AS start_gap,
    MIN(end.height) - 1 AS end_gap
FROM 
    blocks AS start
JOIN 
    blocks AS end
ON 
    start.height < end.height
WHERE 
    NOT EXISTS (
        SELECT 1
        FROM blocks AS mid
        WHERE mid.height = start.height + 1
    )
GROUP BY 
    start.height
HAVING 
    start_gap < MIN(end.height)
ORDER BY 
    start_gap;
